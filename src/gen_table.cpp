// SPDX-FileCopyrightText: 2025 Toshimitsu Kimura <lovesyao@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later
#include <iostream>
#include <boost/multiprecision/cpp_dec_float.hpp>

namespace mp = boost::multiprecision;


// サンプリングレート（48000Hz を想定）
const float sampleRate = 48000.0f;

// 表示用の上限ピッチ（Hz） 
const float maxDisplayPitch = 880.000f; // A6 の周波数

// 表示用の下限ピッチ（Hz） 
const float baseFrequency = 55.0f; // A1 の周波数 (基準音)

const size_t lagMin = ceil(sampleRate / maxDisplayPitch);
const size_t lagMax = floor(sampleRate / baseFrequency) + 1;


int main() {

//float Pitch = sampleRate / bestLag; // bestLag = lagMin..lagMax
// x = ((std::log2(pitch/baseFrequency) / std::log2(maxDisplayPitch / baseFrequency)) * 2.0f - 1.0f);

    std::cout << "// This file is automatically generated from gen_table.cpp" << std::endl;
    std::cout << "float lag_to_y[] = {" << std::endl;
    for (size_t lag = lagMin; lag < lagMax; lag++) {
        auto y = ((mp::log2((mp::cpp_dec_float_100)sampleRate / lag / baseFrequency) / mp::log2((mp::cpp_dec_float_100)maxDisplayPitch / baseFrequency)));
        std::cout << y.str(0, std::ios_base::scientific) << ", " << std::endl;
//        std::cout << mp::log2((mp::cpp_dec_float_100)12.0).str(0, std::ios_base::scientific) << std::endl;
    }
    std::cout << "};" << std::endl;
    std::cout << std::endl;

    std::cout << "float lowpassCoeff[" << lagMax-lagMin << "][" << lagMax << "] = {" << std::endl;
    for (size_t lag = lagMin; lag < lagMax; lag++) {
        std::cout <<  "{" << std::endl; //lowpassCoeff[lag - lagMin][idx];
        size_t idx=0;

        // minimum-phase filterにする？

        // no filter
        for (; idx < 1; idx++) {
           double r;
           r = 1.0;
           std::cout << r << "," << std::endl; //lowpassCoeff[lag - lagMin][idx];
        }

/*
        for (; idx < 2; idx++) {
           double r;
           r = 1.0 / 2;
           std::cout << r << "," << std::endl; //lowpassCoeff[lag - lagMin][idx];
        }
*/

/*
        // Moving Average Filter (2倍幅)
        for (; idx <= std::min(lag*2, lagMax-1); idx++) {
           double r;
           r = 1.0 / (std::min(lag*2, lagMax-1) + 1);
           std::cout << r << "," << std::endl; //lowpassCoeff[lag - lagMin][idx];
        }
*/

/*
        // Moving Average Filter (等幅)
        for (; idx <= lag; idx++) {
           double r;
           r = 1.0 / (lag + 1);
           std::cout << r << "," << std::endl; //lowpassCoeff[lag - lagMin][idx];
        }
*/


/*
//      linear-phase filter
        int64_t n = - (int64_t)lag/2;
        double norm_cutoff = 2 * M_PI * (sampleRate / lag) / sampleRate;

        for (; idx <= lag; idx++, n++) {
            double r;
            if (n == 0)
                r = norm_cutoff / M_PI;
            else
                r = sin(norm_cutoff * n) / (M_PI * n);
           std::cout << r << "," << std::endl; //lowpassCoeff[lag - lagMin][idx];
        }
*/
        for (; idx < lagMax; idx++) {
            std::cout <<  "0.0f" << "," << std::endl;
        }
        std::cout <<  "}," << std::endl;
    }
    std::cout << "};" << std::endl;
    std::cout << std::endl;
}
