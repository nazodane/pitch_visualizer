# コンパイラ
CXX = g++
# コンパイルフラグ
CXXFLAGS = -I/usr/include/spa-0.2/ -I/usr/include/pipewire-0.3/ -Wall -Wextra -O2
# リンクするライブラリ
LDFLAGS = -lglfw -lGLEW -lGL -lpipewire-0.3 -lcap
# 出力ファイル名
TARGET = pitch_visualizer
# ソースファイル
SRC = src/pitch_visualizer.cpp
# インストールディレクトリのルート
DESTDIR = 
# インストールディレクトリのプリフィックス
PREFIX = /usr/local
# インストールディレクトリ
INSTALL_DIR = $(DESTDIR)$(PREFIX)/bin
# インストール先
INSTALL_PATH = $(INSTALL_DIR)/$(TARGET)
# ビルドディレクトリ
BUILDDIR = .

PACKAGE_NAME = pitch-visualizer
VERSION = 1.0

SOURCE_DIR = .
TARBALL = ../$(PACKAGE_NAME)_$(VERSION).orig.tar.gz
DEB_DIR = debian

# ビルドルール
all: $(TARGET)

# コンパイルターゲット
$(BUILDDIR)/$(TARGET): $(SRC) src/lag_to_y.h
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) -o $(BUILDDIR)/$(TARGET)

src/lag_to_y.h: gen_table
	$(BUILDDIR)/gen_table > src/lag_to_y.h

$(BUILDDIR)/gen_table: src/gen_table.cpp
	$(CXX) $(CXXFLAGS) src/gen_table.cpp $(LDFLAGS) -o $(BUILDDIR)/gen_table

# インストールターゲット
install: $(BUILDDIR)/$(TARGET)
	mkdir -p $(INSTALL_DIR)
	cp $(BUILDDIR)/$(TARGET) $(INSTALL_DIR)
	setcap 'cap_sys_nice=eip' $(INSTALL_PATH)

# アンインストールターゲット
uninstall:
	rm -f $(INSTALL_PATH)

# クリーンアップ
clean:
	rm -f $(BUILDDIR)/$(TARGET) src/lag_to_y.h $(BUILDDIR)/gen_table

deb: clean tarball
	debuild -b

tarball:
	tar czf $(TARBALL) $(wildcard src/*.cpp) $(wildcard src/*.h) $(wildcard Makefile) $(wildcard README.md)

.PHONY: all install uninstall clean tarball deb

