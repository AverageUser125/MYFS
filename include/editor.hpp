#pragma once

#define CTRL_KEY(k) ((k) & 0x1f)
#define INVERSE_COLOR "\033[7m"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DELETE
#include <vector>
#include <string>
#include "config.hpp"
#include "myfs.hpp"


int readKey();
void editorStart(MyFs& myfs, const std::string& filenameIn);