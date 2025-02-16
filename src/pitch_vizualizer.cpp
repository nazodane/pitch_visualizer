// SPDX-FileCopyrightText: 2025 Toshimitsu Kimura <lovesyao@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later

// g++ pitch_vizualizer.cpp -I/usr/include/spa-0.2/ -I/usr/include/pipewire-0.3/ -lglfw -lGLEW  -lGL -lpipewire-0.3 -lcap -o pitch_vizualizer
// sudo setcap 'cap_sys_nice=eip' ./pitch_vizualizer

#define ENABLE_REALTIME

#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>

#ifdef ENABLE_REALTIME
#include <sys/mman.h>
#include <sys/capability.h>
#include <sys/resource.h>
#endif

#include <pipewire/pipewire.h>
#include <spa/param/format.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw-utils.h>

// OpenGL 関連ヘッダ
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "lag_to_y.h"

// サンプリングレート（48000Hz を想定）
const float sampleRate = 48000.0f;

// 表示用の上限ピッチ（Hz） 
const float maxDisplayPitch = 880.000f; // A6 の周波数

// 表示用の下限ピッチ（Hz） 
const float baseFrequency = 55.0f; // A1 の周波数 (基準音)

// 現在のピッチ（Hz）のリングバッファ
float currentPitchRing[(size_t)sampleRate] = {0.0};
std::atomic<size_t> currentPitchWriteIndex = 0;
size_t currentPitchReadIndex = 0; // thread unsafe

// グローバルストリームポインタ（on_process 内で使用）
static struct pw_stream* g_stream = nullptr;

const size_t lagMin = ceil(sampleRate / maxDisplayPitch); // 54
const size_t lagMax = floor(sampleRate / baseFrequency) + 1; // 873
double lag_to_correlation[lagMax - lagMin] = {0.0};


// 過去のサンプルを保持するためのバッファ
float previousSamples[(size_t)sampleRate] = {0.0}; // 1秒分のサンプルに対応
size_t previousSamplesLen = 0;

double rmsSQ = 0.0f;
const float amplitudeThreshold = 0.005f; // 小さな音の閾値
float newPitch = 0.0f;

// ピッチを計算
static void on_process([[maybe_unused]] void *userdata) {
    struct pw_stream *stream = g_stream;
    struct pw_buffer *buffer = pw_stream_dequeue_buffer(stream);
    if (buffer == nullptr)
        return;

    if (buffer->buffer->n_datas > 0) {
        struct spa_data *d = &buffer->buffer->datas[0];
        if (d->data == nullptr || d->chunk == nullptr) {
            pw_stream_queue_buffer(stream, buffer);
            return;
        }

        size_t offset = d->chunk->offset;
        size_t size = d->chunk->size;
        size_t numSamples = size / sizeof(float);
        float* audioData = (float*)((uint8_t*)d->data + offset);

/*        if (previousSamplesLen < numSamples){ // 例えばnumSamples=940と941が交互に来る
//            std::cout << "init with " << numSamples << std::endl;
        }
*/

        size_t realLagMax = std::min(numSamples / 2, lagMax);
        ptrdiff_t prevOffset = (ptrdiff_t)previousSamplesLen - numSamples;
        for (size_t t = 0; (ptrdiff_t)t < prevOffset && t < previousSamplesLen; t++){
            rmsSQ -= (double)previousSamples[t] * previousSamples[t];
            for (size_t lag = lagMin; lag < realLagMax && t + lag < previousSamplesLen; lag++) {
                lag_to_correlation[lag] -= (double)previousSamples[t] * previousSamples[t + lag];
            }
        }

        // ここで t を 0 から numSamples まで繰り返してずらしながら処理する
        for (size_t t = 0; t < numSamples; t++) {

            // RMS振幅の計算
            if ((ptrdiff_t)t + prevOffset >= 0 && (ptrdiff_t)t + prevOffset < (ptrdiff_t)previousSamplesLen)
                rmsSQ -= (double)previousSamples[t + prevOffset] * previousSamples[t + prevOffset];
            rmsSQ += (double)audioData[t] * audioData[t];

            // 自己相関法によるピッチ検出
            if ((ptrdiff_t)t + prevOffset >= 0)
                for (size_t lag = lagMin; lag < realLagMax && (ptrdiff_t)t + prevOffset + lag < previousSamplesLen; lag++)
                    lag_to_correlation[lag] -= ((double)previousSamples[t + prevOffset] * previousSamples[t + prevOffset + lag]);

            for (size_t lag = lagMin; lag < realLagMax; lag++)
                lag_to_correlation[lag] += (double)audioData[t] * audioData[t + lag];


/*
                float bestCorrelation = 0.0f;

                size_t bestLag = lagMin;
                size_t secondBesｔLag = lagMin;
                size_t thirdBesｔLag = lagMin;
                for (size_t lag = lagMin; lag < realLagMax; lag++) { // TODO: もっと良い選び方ありそう
                    if (bestCorrelation < lag_to_correlation[lag]) {
                        bestCorrelation = lag_to_correlation[lag];
                        thirdBesｔLag = secondBesｔLag;
                        secondBesｔLag = bestLag;
                        bestLag = lag;
                    }
                }
                float prevNewPitch = newPitch;
                float newFirstPitch = std::log2(sampleRate / bestLag);
                float newSecondPitch = std::log2(sampleRate / secondBesｔLag);
                float newThirdPitch = std::log2(sampleRate / thirdBesｔLag); // TODO
                newPitch = std::abs(prevNewPitch - newFirstPitch) < abs(prevNewPitch - newSecondPitch) ? (
                    std::abs(prevNewPitch - newFirstPitch) < abs(prevNewPitch - newThirdPitch) ? newFirstPitch : newThirdPitch
                    ) : (
                    std::abs(prevNewPitch - newSecondPitch) < abs(prevNewPitch - newThirdPitch) ? newSecondPitch : newThirdPitch
                    );
*/

            if (rmsSQ < amplitudeThreshold * amplitudeThreshold * numSamples) { // 小さい音のピッチは無視してリングバッファに-1を格納する
                currentPitchRing[currentPitchWriteIndex] = -1;
            } else{ // 有効な音はピッチの検出を最後まで進めてリングバッファに格納する
                float bestCorrelation = 0.0f;
                size_t bestLag = lagMin;
                for (size_t lag = lagMin; lag < realLagMax; lag++) { // TODO: もっと良い選び方ありそう
                    if (bestCorrelation < lag_to_correlation[lag]) {
                        bestCorrelation = lag_to_correlation[lag];
                        bestLag = lag;
                    }
                }
                float newPitch = lag_to_y[bestLag - lagMin]; //std::log2(bestLag);
/*
                float prevNewPitch = newPitch;
                size_t bestLag = lagMin;
                for (size_t lag = lagMin; lag < realLagMax; lag++) { // TODO: もっと良い選び方ありそう
                    float tmpPitchDiff = std::abs(prevNewPitch - std::log2(sampleRate / lag));
                    if (bestCorrelation < lag_to_correlation[lag] / tmpPitchDiff) {
                        bestCorrelation = lag_to_correlation[lag] / tmpPitchDiff;
                        bestLag = lag;
                    }
                }
                newPitch = std::log2(sampleRate / bestLag);
*/

                //std::cout << (std::abs(std::log2(prevNewPitch) - std::log2(newFirstPitch)) < abs(std::log2(prevNewPitch) - std::log2(newSecondPitch)))  << std::endl;

                currentPitchRing[currentPitchWriteIndex] = newPitch;
            }
            size_t newWriteIndex = currentPitchWriteIndex.load(std::memory_order_relaxed) + 1;
            if (newWriteIndex >= (size_t)sampleRate)
                newWriteIndex -= (size_t)sampleRate;
            currentPitchWriteIndex.store(newWriteIndex, std::memory_order_release);
        }
        memcpy(&previousSamples[0], audioData, numSamples*sizeof(float));
        previousSamplesLen = numSamples;
//        assert(memcmp(&previousSamples[0], audioData, numSamples*sizeof(float)) == 0);
    }
    pw_stream_queue_buffer(stream, buffer);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// Pipewire のストリームイベント構造体
static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};
#pragma GCC diagnostic pop

const size_t maxHistory = 10 * (size_t)sampleRate; // 表示するサンプルの数（10秒分）
std::vector<GLfloat> vertices(maxHistory*2);
std::vector<GLuint> indices(maxHistory);
GLuint vao, vbo, ebo;
GLuint shaderProgram;

// 頂点シェーダー
const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

// フラグメントシェーダー
const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    void main() {
        FragColor = vec4(0.0, 1.0, 0.0, 0.0);
    }
)";

GLuint createShaderProgram() {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

void framebuffer_size_callback([[maybe_unused]] GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);  // OpenGLのビューポートを更新
    std::cout << "Window resized: " << width << " x " << height << std::endl;
}

// ウインドウ関係
bool isFullscreen = false;
GLFWmonitor* primaryMonitor;
int windowX, windowY, windowWidth, windowHeight; // フルスクリーンにする前のウインドウの状態

// フルスクリーンのトグル
void toggleFullscreen(GLFWwindow* window) {
    if (isFullscreen) { // フルスクリーンからウインドウへ
        glfwSetWindowMonitor(window, nullptr, windowX, windowY, windowWidth, windowHeight, 0);
    } else { // ウインドウからフルスクリーンへ
        primaryMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor); // TODO: 現在はプライマリモニタのみ

        // ウィンドウの位置とサイズを保存
        glfwGetWindowPos(window, &windowX, &windowY);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);

        // フルスクリーン化
        glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    isFullscreen = !isFullscreen;
}

// キーボード入力のコールバック関数
void key_callback(GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods) {
    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        toggleFullscreen(window);
    } else if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true); // ESCでウィンドウを閉じる
    }
}

// OpenGL の初期化（GLFW ウィンドウの作成）
void initOpenGL(GLFWwindow** window) {
    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed. exit." << std::endl;
        exit(EXIT_FAILURE);
    }
    *window = glfwCreateWindow(800, 600, "Vocal Pitch Visualizer", nullptr, nullptr);
    if (!*window) {
        std::cerr << "GLFW window creation failed. exit." << std::endl;
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(*window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW initialization failed. exit." << std::endl;
        exit(EXIT_FAILURE);
    }

    glfwSetFramebufferSizeCallback(*window, framebuffer_size_callback); // ウインドウリサイズのコールバックを登録
    glfwSetKeyCallback(*window, key_callback); // キー入力のコールバックを登録

    shaderProgram = createShaderProgram();

    glEnable(GL_PRIMITIVE_RESTART);
    glPrimitiveRestartIndex(0xFFFF);  // 使わないインデックスを設定

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    // 頂点バッファ
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);

    // インデックスバッファ
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    // 頂点属性
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(0);

    glUseProgram(shaderProgram);
}

// baseFrequency を基に全音と半音を算出
float calculateNoteFrequency(float baseFrequency, int semitoneOffset) {
    return baseFrequency * std::pow(2.0f, semitoneOffset / 12.0f);
}

void renderNotes(float baseFrequency, float maxDisplayPitch) {

    for (int semitone = 0; semitone <= 48; ++semitone) {
        // 音程周波数を算出
        float freq = calculateNoteFrequency(baseFrequency, semitone);
        if (semitone % 12 == 0) glColor3f(1.0f, 1.0f, 1.0f); // 白色で基準線を描画
        else if (semitone % 12 == 1) glColor3f(1.0f, 0.0f, 0.0f); // 赤色で基準線を描画

        // 周波数が表示範囲内であれば対応するy軸の位置を計算して描画
        if (freq <= maxDisplayPitch) {
            float y = -1.0f + 2.0f * (std::log2(freq/baseFrequency) / std::log2(maxDisplayPitch / baseFrequency)); // 横軸に音程を対応させる
            glBegin(GL_LINES);
            glVertex2f(-1.0f, y);  // x は左端
            glVertex2f( 1.0f, y);   // x は右端
            glEnd();
        }
    }
}


// OpenGL のレンダリングループ（x方向は時間軸、y方向はピッチ）
void renderLoop(GLFWwindow* window) {
    size_t histIndex = 0;

    while (!glfwWindowShouldClose(window)) {
//        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        //float pitchx = currentPitchRing[currentPitchReadIndex].load();
        //std::cout << "Reading from currentPitchRing[" << currentPitchReadIndex << "]: " << pitchx << std::endl;

        glUseProgram(0);
        // 基準線を描画
        renderNotes(baseFrequency, maxDisplayPitch); 
   
        GLfloat* mappedVbo = (GLfloat*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);  // バッファをマッピングして書き込み可能にする
        GLuint* mappedEbo = (GLuint*)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);

        mappedEbo[histIndex] = histIndex; // 前回の接続線を再接続

        while (currentPitchReadIndex != currentPitchWriteIndex) {
            // 現在のピッチ値を取得（音量が小さい場合、-1が格納されている）
            float pitch_y = currentPitchRing[currentPitchReadIndex];
            currentPitchReadIndex++;
            if (currentPitchReadIndex >= (size_t)sampleRate)
                currentPitchReadIndex -= (size_t)sampleRate;

            // vboにx座標を入れる
            mappedVbo[histIndex*2 + 0] = -1.0f + 2.0f * (float(histIndex) / (maxHistory - 1));
            // vboにy座標を入れる
            // y軸は 0Hz -> -1, maxDisplayPitch -> 1　の対数マッピング
//            mappedVbo[histIndex*2 + 1] = pitch_log2 == -1 ? -1 : (((std::log2(sampleRate) - pitch_log2 - std::log2(baseFrequency)) / std::log2(maxDisplayPitch / baseFrequency)) * 2.0f - 1.0f);
            mappedVbo[histIndex*2 + 1] = pitch_y == -1 ? -1 : pitch_y;
            if (pitch_y == -1.0f) mappedEbo[histIndex] = 0xFFFF; 
            else mappedEbo[histIndex] = histIndex;
            histIndex++;
            if (histIndex >= maxHistory) histIndex -= maxHistory;
        }

        mappedEbo[histIndex] = 0xFFFF; // 接続線を切る

        glUseProgram(shaderProgram);
//        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW); // TODO: Use glBufferSubData
//        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW); // TODO: Use glBufferSubData
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

        glDrawElements(GL_LINE_STRIP, indices.size(), GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
}


#ifdef ENABLE_REALTIME
bool has_cap(cap_value_t cap) {
    cap_t caps = cap_get_proc();  // 現在のプロセスの権限セットを取得
    if (caps == NULL) {
        perror("cap_get_proc");
        return false;
    }
    cap_flag_value_t value;
    int err = cap_get_flag(caps, cap, CAP_EFFECTIVE, &value);  // 権限をチェック
    cap_free(caps);  // メモリ解放

    return (err == 0 && value == CAP_SET);  // 権限が有効なら true を返す
}
#endif

int main() {
    int err = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (err == 0)
        std::cout << "mlockall(MCL_CURRENT | MCL_FUTURE) is succeed!" << std::endl;
    else
        std::cout << "mlockall(MCL_CURRENT | MCL_FUTURE) is failed but continue anyway!" << std::endl;

#ifdef ENABLE_REALTIME

    if (has_cap(CAP_SYS_NICE)) {
        std::cout << "CAP_SYS_NICE is enabled! nice!" << std::endl;
        pid_t pgid = getpgrp();
        // nice値の設定
        if (setpriority(PRIO_PGRP, pgid, -20) < 0)
            std::cerr << "setpriority failed: " << strerror(errno) << ", but continue anyway!" << std::endl;
        else 
            std::cout << "Priority set to -20 for process group " << pgid << "! nice!" << std::endl;

        pid_t pid = getpid();
        // スケジューラーの設定
        struct sched_param sp = { .sched_priority = 19 };
         if (sched_setscheduler(pid, SCHED_FIFO, &sp) < 0)
            std::cerr << "sched_setscheduler failed: " << strerror(errno) << ", but continue anyway!" << std::endl;
        else 
            std::cout << "Scheduler set to SCHED_FIFO for process " << pid << "! nice!" << std::endl;
       

    } else {
        std::cout << "CAP_SYS_NICE is not enabled but continue anyway!" << std::endl;
    }
    
#endif

    // Pipewire の初期化
    pw_init(nullptr, nullptr);
    
    // メインループとコンテキストの生成
    struct pw_main_loop *pw_loop = pw_main_loop_new(nullptr);
    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(pw_loop), nullptr, 0);
    
    // spa_pod_builder を用いて音声フォーマットのパラメータを生成
    uint8_t pod_buffer[1024];
    struct spa_pod_builder builder;
    spa_pod_builder_init(&builder, pod_buffer, sizeof(pod_buffer));
    
    // spa_audio_info_raw に必要なパラメータをセット
    struct spa_audio_info_raw info;
    memset(&info, 0, sizeof(info));
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = (size_t)sampleRate;
    info.channels = 1;
    
    // 音声フォーマットのパラメータ作成
    struct spa_pod *params = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
    
    // Pipewire ストリーム生成
    g_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(pw_loop),
        "PitchDetector",
        nullptr,       // properties
        &stream_events,
        nullptr        // user data
    );
    if (!g_stream) {
        std::cerr << "Pipewire ストリーム生成失敗" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // ストリームを入力方向で接続
    const spa_pod *cparams = params;
    int res = pw_stream_connect(
        g_stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        &cparams,
        1
    );
    if (res < 0) {
        std::cerr << "Pipewireストリームの接続に失敗" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // Pipewire メインループを別スレッドで実行
    std::thread pipewireThread([&](){
        pw_main_loop_run(pw_loop);
    });
    
    // OpenGL 初期化とレンダリングループ
    GLFWwindow* window = nullptr;
    initOpenGL(&window);
    renderLoop(window);
    
    // ウィンドウが閉じられたら Pipewire ループを終了
    pw_main_loop_quit(pw_loop);
    pipewireThread.join();
    
    // リソース解放
    pw_stream_destroy(g_stream);
    pw_context_destroy(context);
    pw_main_loop_destroy(pw_loop);
    pw_deinit();

#ifdef ENABLE_REALTIME
    munlockall();
#endif

    return 0;
}

