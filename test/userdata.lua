
local ud = newproxy(true)

local m = getmetatable(ud)

m.__index = m
m.__newindex = m

m.X = 1
m.Y = 1

local a = ud.X

local ITER = 1000000

print(ud.X)

local function test2(p1, p2)
  p1.X = p1.X + p2.X
  p1.Y = p1.Y + p2.Y
  return p1
end

local start = os.clock()
for i = 1, ITER do
--   ud.x = 555
  local xx = ud.X
end
print(os.clock() - start)

do
  local po1 = newproxy(true)
  local po2 = newproxy(true)
  local m1 = getmetatable(po1)
  local m2 = getmetatable(po2)
  m1.__index = m1
  m1.__newindex = m1
  m2.__index = m2
  m2.__newindex = m2
  m1.X = 1
  m1.Y = 2
  m2.X = 3
  m2.Y = 4

  print(po1.X, po1.Y, po2.X, po2.Y)
  po1.X = 2
  print(po1.X, po1.Y, po2.X, po2.Y)

  local t = os.clock()
  for i = 1, ITER do
    local p = test2(po1, po2)
  end
  print(os.clock() - t)
  print(po1.X, po1.Y)
end

