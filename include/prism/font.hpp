#pragma once
// Built-in 5x7 bitmap font (ASCII; lowercase is drawn as uppercase). Each glyph occupies a
// 6x8 cell in the generated atlas. The engine uploads the atlas as an ordinary texture.
#include "image.hpp"

namespace prism {

constexpr int FONT_CELL_W = 6;
constexpr int FONT_CELL_H = 8;

Image make_font_atlas();            // 16x6 cells covering ASCII 32..127, white on transparent
IRect font_glyph_rect(char c);      // cell rectangle for a character inside the atlas

}  // namespace prism
