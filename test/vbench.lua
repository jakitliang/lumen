
local NUM_ITERATIONS = 1000000

local function test1(x1, y1, x2, y2)
  return x1 + x2, y1 + y2
end

local function test2(p1, p2)
  p1.X = p1.X + p2.X
  p1.Y = p1.Y + p2.Y
  return p1
end

local Point = {}
Point.__index = Point
Point.__add = function (lhs, rhs)
  lhs.X = lhs.X + rhs.X
  lhs.Y = lhs.Y + rhs.Y
  return lhs
end

do
  local x1, y1 = 1, 2
  local x2, y2 = 3, 4

  local t = os.clock()
  for i = 1, NUM_ITERATIONS do
    x1, y1 = test1(x1, y1, x2, y2)
  end
  print(os.clock() - t)
  print(x1, y1)
end

do
  local p1 = {}
  local p2 = {}
  p1.X = 1
  p1.Y = 2
  p2.X = 3
  p2.Y = 4

  local t = os.clock()
  for i = 1, NUM_ITERATIONS do
    local p = test2(p1, p2)
  end
  print(os.clock() - t)
  print(p1.X, p1.Y)
end

do
  local po1 = setmetatable({}, Point)
  po1.X = 1
  po1.Y = 2
  local po2 = setmetatable({}, Point)
  po2.X = 3
  po2.Y = 4

  local t = os.clock()
  for i = 1, NUM_ITERATIONS do
    local p = po1 + po2
  end
  print(os.clock() - t)
  print(po1.X, po1.Y)
end

do
  local po1 = setmetatable({}, Point)
  po1.X = 1
  po1.Y = 2
  local po2 = setmetatable({}, Point)
  po2.X = 3
  po2.Y = 4

  local t = os.clock()
  for i = 1, NUM_ITERATIONS do
    local p = test2(po1, po2)
  end
  print(os.clock() - t)
  print(po1.X, po1.Y)
end
