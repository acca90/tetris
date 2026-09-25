-- Generates every pixel-art asset for Pixel Boy Tetris.
-- Run headless from the project dir:
--   aseprite -b --script-param root=$PWD --script art/make-sprites.lua
-- Each asset is saved as art/<name>.aseprite (editable) + assets/<name>.png.
-- assets/layout.js carries the device coordinates the game needs (screen, buttons).

local root = app.params["root"] or "."
local pc = app.pixelColor

-- Palette -----------------------------------------------------------------
local NAVY    = 0x1d2438
local CREAM   = 0xf1e8cc
local HILITE  = 0xfffaeb
local CSHADE  = 0xd9cca6
local SIDE    = 0xbfae84
local INK     = 0x7d7057   -- printed labels on the body
local BEZEL   = 0x3e4865
local CORAL   = 0xe8566c
local ORANGE  = 0xf28c3b
local MUSTARD = 0xf2a93b
local YELLOW  = 0xf6d45a
local SAGE    = 0x7fbf6a
local TEAL    = 0x3fb7c9
local BLUE    = 0x4a7fd4
local LAV     = 0x9a82e0
local PINK    = 0xf28aa0

local function ch(h, s) return (h >> s) & 0xff end
local function mix(a, b, t)
  local function m(s) return math.floor(ch(a, s) + (ch(b, s) - ch(a, s)) * t + 0.5) end
  return (m(16) << 16) | (m(8) << 8) | m(0)
end
local function lighten(h, t) return mix(h, 0xffffff, t) end
local function darken(h, t) return mix(h, NAVY, t) end

-- Tiny raster canvas --------------------------------------------------------
local function Canvas(w, h)
  local c = { w = w, h = h, img = Image(w, h, ColorMode.RGB) }
  function c:px(x, y, col, a)
    if x >= 0 and y >= 0 and x < self.w and y < self.h then
      self.img:drawPixel(x, y, pc.rgba(ch(col, 16), ch(col, 8), ch(col, 0), a or 255))
    end
  end
  function c:rect(x, y, rw, rh, col, a)
    for yy = y, y + rh - 1 do for xx = x, x + rw - 1 do self:px(xx, yy, col, a) end end
  end
  -- filled rounded rect; r = number or {tl, tr, br, bl}
  function c:rrect(x, y, rw, rh, r, col)
    if type(r) == "number" then r = { r, r, r, r } end
    local function inC(xx, yy, rad, cx, cy)
      local dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
      return dx * dx + dy * dy <= rad * rad
    end
    for yy = 0, rh - 1 do
      for xx = 0, rw - 1 do
        local ok = true
        if xx < r[1] and yy < r[1] then ok = inC(xx, yy, r[1], r[1], r[1])
        elseif xx >= rw - r[2] and yy < r[2] then ok = inC(xx, yy, r[2], rw - r[2], r[2])
        elseif xx >= rw - r[3] and yy >= rh - r[3] then ok = inC(xx, yy, r[3], rw - r[3], rh - r[3])
        elseif xx < r[4] and yy >= rh - r[4] then ok = inC(xx, yy, r[4], r[4], rh - r[4]) end
        if ok then self:px(x + xx, y + yy, col) end
      end
    end
  end
  function c:circle(cx, cy, rad, col)
    for yy = math.floor(cy - rad - 1), math.ceil(cy + rad + 1) do
      for xx = math.floor(cx - rad - 1), math.ceil(cx + rad + 1) do
        local dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
        if dx * dx + dy * dy <= rad * rad then self:px(xx, yy, col) end
      end
    end
  end
  function c:save(name)
    local s = Sprite(self.w, self.h, ColorMode.RGB)
    s.cels[1].image = self.img
    s:saveAs(root .. "/art/" .. name .. ".aseprite")
    s:saveCopyAs(root .. "/assets/" .. name .. ".png")
    s:close()
  end
  return c
end

-- 3x5 pixel font ------------------------------------------------------------
local FONT_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!-.:/?'+<>@*"
local G = {
  [" "] = "... ... ... ... ...",
  A = ".#. #.# ### #.# #.#", B = "##. #.# ##. #.# ##.", C = ".## #.. #.. #.. .##",
  D = "##. #.# #.# #.# ##.", E = "### #.. ##. #.. ###", F = "### #.. ##. #.. #..",
  G = ".## #.. #.# #.# .##", H = "#.# #.# ### #.# #.#", I = "### .#. .#. .#. ###",
  J = "..# ..# ..# #.# .#.", K = "#.# #.# ##. #.# #.#", L = "#.. #.. #.. #.. ###",
  M = "#.# ### ### #.# #.#", N = "##. #.# #.# #.# #.#", O = ".#. #.# #.# #.# .#.",
  P = "##. #.# ##. #.. #..", Q = ".#. #.# #.# ##. .##", R = "##. #.# ##. #.# #.#",
  S = ".## #.. .#. ..# ##.", T = "### .#. .#. .#. .#.", U = "#.# #.# #.# #.# ###",
  V = "#.# #.# #.# .#. .#.", W = "#.# #.# ### ### #.#", X = "#.# #.# .#. #.# #.#",
  Y = "#.# #.# .#. .#. .#.", Z = "### ..# .#. #.. ###",
  ["0"] = "### #.# #.# #.# ###", ["1"] = ".#. ##. .#. .#. ###", ["2"] = "##. ..# .#. #.. ###",
  ["3"] = "##. ..# .#. ..# ##.", ["4"] = "#.# #.# ### ..# ..#", ["5"] = "### #.. ##. ..# ##.",
  ["6"] = ".## #.. ### #.# ###", ["7"] = "### ..# .#. .#. .#.", ["8"] = "### #.# ### #.# ###",
  ["9"] = "### #.# ### ..# ##.",
  ["!"] = ".#. .#. .#. ... .#.", ["-"] = "... ... ### ... ...", ["."] = "... ... ... ... .#.",
  [":"] = "... .#. ... .#. ...", ["/"] = "..# ..# .#. #.. #..", ["?"] = "##. ..# .#. ... .#.",
  ["'"] = ".#. .#. ... ... ...", ["+"] = "... .#. ### .#. ...", ["<"] = "..# .#. #.. .#. ..#",
  [">"] = "#.. .#. ..# .#. #..", ["@"] = "... #.# ### ### .#.", ["*"] = "#.# .#. #.# ... ...",
}
local function glyph(chr)
  local rows = {}
  for row in (G[chr] or G[" "]):gmatch("%S+") do rows[#rows + 1] = row end
  return rows
end
-- scale 1/2; italic shears rows right toward the top
local function text(cv, str, x, y, col, scale, italic)
  scale = scale or 1
  for i = 1, #str do
    local rows = glyph(str:sub(i, i):upper())
    local gx = x + (i - 1) * 4 * scale
    for r = 1, 5 do
      for cc = 1, 3 do
        if rows[r]:sub(cc, cc) == "#" then
          for sy = 0, scale - 1 do
            local py = (r - 1) * scale + sy
            local shear = italic and math.floor((5 * scale - 1 - py) / 4) or 0
            for sx = 0, scale - 1 do cv:px(gx + (cc - 1) * scale + sx + shear, y + py, col) end
          end
        end
      end
    end
  end
end
local function textW(str, scale) return #str * 4 * (scale or 1) - (scale or 1) end

local font = Canvas(#FONT_CHARS * 3, 5)
for i = 1, #FONT_CHARS do text(font, FONT_CHARS:sub(i, i), (i - 1) * 3, 0, 0xffffff) end
font:save("font")

-- Tiles: I O T S Z J L, ghost, dead -----------------------------------------
-- I, O and T get the hollow NES-style centre; S Z J L are solid bevels.
local PIECES = { TEAL, YELLOW, LAV, SAGE, CORAL, BLUE, ORANGE }
local HOLLOW = { true, true, true, false, false, false, false }
local tiles = Canvas(8 * 9, 8)
local function block(cv, ox, base, hollow)
  local hi, lo, out, shine = lighten(base, 0.45), darken(base, 0.3), darken(base, 0.62), lighten(base, 0.85)
  for y = 0, 7 do
    for x = 0, 7 do
      local c = base
      if x == 7 or y == 7 then c = out
      elseif x == 0 or y == 0 then c = hi
      elseif x == 6 or y == 6 then c = lo
      elseif hollow and x >= 2 and x <= 5 and y >= 2 and y <= 5 then c = lighten(base, 0.72) end
      cv:px(ox + x, y, c)
    end
  end
  cv:px(ox + 1, 1, shine)
  if not hollow then cv:px(ox + 2, 1, shine); cv:px(ox + 1, 2, shine) end
end
for i, col in ipairs(PIECES) do block(tiles, (i - 1) * 8, col, HOLLOW[i]) end
for y = 0, 7 do
  for x = 0, 7 do
    local edge = x == 0 or y == 0 or x == 7 or y == 7
    if edge and (x + y) % 2 == 0 then tiles:px(56 + x, y, 0xb9c2e6, 190)
    elseif not edge then tiles:px(56 + x, y, 0xb9c2e6, 22) end
  end
end
block(tiles, 64, 0x4a5575, false)
tiles:save("tiles")

-- Logo: "TETRIS" in mini blocks ----------------------------------------------
local LF = {
  T = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." },
  E = { "#####", "#....", "#....", "####.", "#....", "#....", "#####" },
  R = { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" },
  I = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" },
  S = { ".####", "#....", "#....", ".###.", "....#", "....#", "####." },
}
local WORD = { "T", "E", "T", "R", "I", "S" }
local LCOL = { CORAL, ORANGE, YELLOW, SAGE, TEAL, LAV }
local P, LW, GAP = 4, 20, 3
local logo = Canvas(#WORD * LW + (#WORD - 1) * GAP + 2, 7 * P + 2)
for pass = 1, 2 do
  for li, chr in ipairs(WORD) do
    local base, ox = LCOL[li], (li - 1) * (LW + GAP)
    for r, row in ipairs(LF[chr]) do
      for cc = 1, 5 do
        if row:sub(cc, cc) == "#" then
          local x0, y0 = ox + (cc - 1) * P, (r - 1) * P
          for y = 0, P - 1 do for x = 0, P - 1 do
            if pass == 1 then logo:px(x0 + x + 2, y0 + y + 2, 0x0c1020)
            else
              local col = base
              if x == P - 1 or y == P - 1 then col = darken(base, 0.45)
              elseif x == 0 or y == 0 then col = lighten(base, 0.45) end
              logo:px(x0 + x, y0 + y, col)
            end
          end end
        end
      end
    end
  end
end
logo:save("logo")

-- Device ------------------------------------------------------------------------
local W, H = 236, 308
local FACE_R = { 9, 9, 31, 9 }
local LCD = { x = 28, y = 30, w = 176, h = 168 }
local DPAD = { cx = 42, cy = 254 }
local BTN_B = { cx = 174, cy = 262 }
local BTN_A = { cx = 202, cy = 250 }
local SEL = { x = 92, y = 284 }
local STA = { x = 118, y = 284 }

local function drawDevice(pressed)
  local cv = Canvas(W, H)
  -- extruded side: the face shape swept 3px down-right
  for d = 0, 3 do cv:rrect(d, d, 233, 305, FACE_R, NAVY) end
  for d = 1, 3 do cv:rrect(d + 1, d + 1, 231, 303, { 8, 8, 30, 8 }, SIDE) end
  -- face
  cv:rrect(0, 0, 233, 305, FACE_R, NAVY)
  cv:rrect(1, 1, 231, 303, { 8, 8, 30, 8 }, HILITE)
  cv:rrect(2, 2, 230, 302, { 8, 8, 30, 8 }, CSHADE)
  cv:rrect(2, 2, 229, 301, { 8, 8, 30, 8 }, CREAM)
  -- top ridge + power switch print
  cv:rect(8, 11, 217, 1, CSHADE); cv:rect(8, 12, 217, 1, HILITE)
  text(cv, "<OFF ON>", 12, 4, 0xb0a27a)
  cv:rect(204, 3, 18, 5, CSHADE); cv:rect(205, 4, 7, 3, pressed and TEAL or SIDE)

  -- bezel
  cv:rrect(12, 16, 208, 190, { 6, 6, 22, 6 }, NAVY)
  cv:rrect(13, 17, 206, 188, { 5, 5, 21, 5 }, 0x56617f)
  cv:rrect(13, 18, 206, 187, { 5, 5, 21, 5 }, BEZEL)
  local label = "DOT MATRIX WITH STEREO SOUND"
  local lx = 116 - (textW(label) // 2)
  text(cv, label, lx, 21, 0xc9cfe6)
  cv:rect(18, 22, lx - 22, 1, CORAL);  cv:rect(18, 24, lx - 22, 1, TEAL)
  local rx = lx + textW(label) + 4
  cv:rect(rx, 22, 214 - rx, 1, CORAL); cv:rect(rx, 24, 214 - rx, 1, TEAL)
  -- screen well (the game draws the LCD itself)
  cv:rect(LCD.x - 1, LCD.y - 1, LCD.w + 2, LCD.h + 2, 0x0c1020)
  cv:rect(LCD.x, LCD.y, LCD.w, LCD.h, 0x1b2340)
  -- power LED
  cv:circle(20.5, 100.5, 3, 0x2a2f45)
  cv:circle(20.5, 100.5, 2.2, pressed and 0xff5a73 or 0x5b2c3c)
  if pressed then cv:px(19, 99, 0xffd0d8) end
  text(cv, "PWR", 15, 107, 0x9aa3c4)

  -- brand: rainbow PIXEL with navy drop shadow + bold navy BOY
  local bx = 14
  for i = 1, 5 do
    local chr = ("PIXEL"):sub(i, i)
    text(cv, chr, bx + (i - 1) * 8 + 1, 212, NAVY, 2)
    text(cv, chr, bx + (i - 1) * 8, 211, ({ CORAL, ORANGE, SAGE, TEAL, LAV })[i], 2)
  end
  text(cv, "BOY", bx + 46, 211, NAVY, 2)
  text(cv, "BOY", bx + 47, 211, NAVY, 2)
  -- heart sticker
  local HEART = { ".NN...NN.", "NPPN.NPPN", "NWPPNPPPN", "NPPPPPPPN", ".NPPPPPN.", "..NPPPN..", "...NPN...", "....N...." }
  local HC = { N = NAVY, P = PINK, W = HILITE }
  for y, row in ipairs(HEART) do
    for x = 1, #row do local k = row:sub(x, x); if HC[k] then cv:px(98 + x, 211 + y, HC[k]) end end
  end

  -- d-pad
  local cx, cy = DPAD.cx, DPAD.cy
  local function cross(ox, oy, pad, col)
    cv:rect(cx - 5 - pad + ox, cy - 15 - pad + oy, 10 + 2 * pad, 30 + 2 * pad, col)
    cv:rect(cx - 15 - pad + ox, cy - 5 - pad + oy, 30 + 2 * pad, 10 + 2 * pad, col)
  end
  cv:circle(cx, cy, 20, CSHADE)
  cv:circle(cx, cy, 19, 0xe6dcbc)
  cross(1, 2, 1, darken(CSHADE, 0.25))
  cross(0, 0, 1, NAVY)
  cross(0, 0, 0, 0x2e3754)
  -- arm highlights + arrow marks; pressed arms go dark with a lit teal arrow
  local arms = {
    { x = cx - 5, y = cy - 15, w = 10, h = 10, tri = { { 0, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 } } },
    { x = cx - 5, y = cy + 5,  w = 10, h = 10, tri = { { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } } },
    { x = cx - 15, y = cy - 5, w = 10, h = 10, tri = { { -1, 0 }, { 0, -1 }, { 0, 0 }, { 0, 1 } } },
    { x = cx + 5, y = cy - 5,  w = 10, h = 10, tri = { { 1, 0 }, { 0, -1 }, { 0, 0 }, { 0, 1 } } },
  }
  for _, a in ipairs(arms) do
    if pressed then cv:rect(a.x, a.y, a.w, a.h, 0x1f2640)
    else cv:rect(a.x, a.y, a.w, 1, 0x4b5679) end
    local mx, my = a.x + 5, a.y + 5
    for _, t in ipairs(a.tri) do cv:px(mx + t[1], my + t[2], pressed and 0x6fcad6 or 0x5a668c) end
  end
  cv:circle(cx, cy, 2.5, 0x252d45)

  -- A / B
  local function button(b, col)
    cv:circle(b.cx, b.cy, 12, CSHADE)
    cv:circle(b.cx, b.cy, 11, 0xe6dcbc)
    if pressed then
      cv:circle(b.cx, b.cy + 1, 9, NAVY)
      cv:circle(b.cx, b.cy + 1, 8, darken(col, 0.3))
    else
      cv:circle(b.cx, b.cy + 2, 9, NAVY)
      cv:circle(b.cx, b.cy, 9, NAVY)
      cv:circle(b.cx, b.cy, 8, col)
      cv:circle(b.cx - 2, b.cy - 2, 4, lighten(col, 0.25))
      cv:circle(b.cx - 1, b.cy - 1, 4.5, col)
      cv:px(b.cx - 5, b.cy - 3, lighten(col, 0.7)); cv:px(b.cx - 4, b.cy - 4, lighten(col, 0.7))
      cv:px(b.cx - 3, b.cy - 5, lighten(col, 0.7))
    end
  end
  button(BTN_B, MUSTARD)
  button(BTN_A, CORAL)
  text(cv, "HOLD", BTN_B.cx - textW("HOLD") // 2, 276, INK)
  text(cv, "DROP", BTN_A.cx - textW("DROP") // 2, 264, INK)

  -- select / start pills
  local function pill(p, lbl)
    if pressed then
      cv:rrect(p.x, p.y + 1, 18, 6, 3, NAVY)
      cv:rrect(p.x + 1, p.y + 2, 16, 4, 2, 0x646d8c)
    else
      cv:rrect(p.x, p.y + 1, 18, 6, 3, NAVY)
      cv:rrect(p.x, p.y, 18, 6, 3, NAVY)
      cv:rrect(p.x + 1, p.y + 1, 16, 4, 2, 0x8a93b2)
      cv:rect(p.x + 3, p.y + 1, 12, 1, 0xaeb6d0)
    end
    text(cv, lbl, p.x + 9 - textW(lbl) // 2, 293, INK)
  end
  pill(SEL, "SELECT")
  pill(STA, "START")

  -- speaker grille
  for i = 0, 5 do
    local sx, sy = 150 + i * 7, 300
    for k = 0, 13 do
      cv:px(sx + k, sy - k, 0x4a4252); cv:px(sx + k + 1, sy - k, 0x4a4252)
      cv:px(sx + k + 2, sy - k, HILITE)
    end
  end
  return cv
end
drawDevice(false):save("device")
drawDevice(true):save("device-pressed")

-- Page stickers ----------------------------------------------------------------
local function sparkle(col)
  local cv = Canvas(22, 11)
  for f = 0, 1 do
    for y = 0, 10 do
      for x = 0, 10 do
        local dx, dy = math.abs(x - 5), math.abs(y - 5)
        local on, white
        if f == 0 then
          on = (dx == 0 and dy <= 5) or (dy == 0 and dx <= 5) or (dx <= 1 and dy <= 2) or (dy <= 1 and dx <= 2)
          white = dx + dy <= 1
        else
          on = (dx == 0 and dy <= 3) or (dy == 0 and dx <= 3) or (dx <= 1 and dy <= 1)
          white = dx + dy == 0
        end
        if on then cv:px(f * 11 + x, y, white and 0xffffff or col) end
      end
    end
  end
  return cv
end
sparkle(YELLOW):save("spark-yellow")
sparkle(TEAL):save("spark-teal")
sparkle(PINK):save("spark-pink")

local CURSOR = {
  "N.......", "NN......", "NWN.....", "NWWN....", "NWWWN...", "NWWWWN..", "NWWWWWN.",
  "NWWWWWWN", "NWWWNNNN", "NWNWN...", "NN.NWN..", "N...NWN.", ".....NN.",
}
local cur = Canvas(8, 13)
for y, row in ipairs(CURSOR) do
  for x = 1, #row do
    local k = row:sub(x, x)
    if k == "N" then cur:px(x - 1, y - 1, 0x0c1020) elseif k == "W" then cur:px(x - 1, y - 1, CREAM) end
  end
end
cur:save("cursor")

-- Layout for the game ------------------------------------------------------------
local function r(x, y, w, h) return string.format("{x:%d,y:%d,w:%d,h:%d}", x, y, w, h) end
local f = io.open(root .. "/assets/layout.js", "w")
f:write("// generated by art/make-sprites.lua — do not edit by hand\n")
f:write("window.LAYOUT = {\n")
f:write(string.format("  W: %d, H: %d,\n", W, H))
f:write("  lcd: " .. r(LCD.x, LCD.y, LCD.w, LCD.h) .. ",\n")
f:write("  led: " .. r(16, 96, 10, 10) .. ",\n")
f:write("  power: " .. r(204, 3, 18, 5) .. ",\n")
f:write("  fontChars: " .. string.format("%q", FONT_CHARS) .. ",\n")
f:write("  controls: {\n")
f:write("    up: " .. r(DPAD.cx - 6, DPAD.cy - 16, 12, 11) .. ",\n")
f:write("    down: " .. r(DPAD.cx - 6, DPAD.cy + 5, 12, 12) .. ",\n")
f:write("    left: " .. r(DPAD.cx - 16, DPAD.cy - 6, 11, 12) .. ",\n")
f:write("    right: " .. r(DPAD.cx + 5, DPAD.cy - 6, 12, 12) .. ",\n")
f:write("    b: " .. r(BTN_B.cx - 10, BTN_B.cy - 10, 21, 23) .. ",\n")
f:write("    a: " .. r(BTN_A.cx - 10, BTN_A.cy - 10, 21, 23) .. ",\n")
f:write("    select: " .. r(SEL.x - 1, SEL.y - 1, 20, 9) .. ",\n")
f:write("    start: " .. r(STA.x - 1, STA.y - 1, 20, 9) .. ",\n")
f:write("  },\n};\n")
f:close()
