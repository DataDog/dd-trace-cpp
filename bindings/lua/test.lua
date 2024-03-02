local ddtrace = require "ddtrace"

function sleep(span, n)
  local span = span:create_child("sleep" .. tostring(n))
  print(span)
  os.execute("sleep " .. tonumber(n))
  print("sleeping for " .. n .. "sec")
  span:finish()
end

local config = {
  ["service"] = "lua-dmehala",
  ["env"] = "dev",
  ["version"] = "0.0.1"
}

local tracer = ddtrace.make_tracer(config)

local span = tracer:create_span("first")
-- print(span)
--
-- sleep(span, 0.1)
-- sleep(span, 0.2)
-- sleep(span, 0.4)
-- sleep(span, 0.8)

local function header_reader(k)
  print("[header_reader] k: " .. k)
  return nil
end

local function header_writer(k, v)
  print("[header_writer] k: " .. k .. ", v: " .. v)
end

span:inject(header_writer)


local new_span = tracer:extract_span(header_reader)
-- local new_span, err = tracer:extract_span(header_reader)
-- span:inject(header_writer)

-- local child = span.create_child()
-- sleep(tracer, 1)

local function read()
  print("Enter text (press Ctrl+D to exit):")
  -- Loop until Ctrl+D (EOF) is pressed
  while true do
    local input = io.read() -- Wait for user input
    if input == nil then
      break                 -- Exit the loop if Ctrl+D (EOF) is pressed
    end
    local span = tracer:create_span(input)
    print("You entered:", input) -- Print the input
    span:finish()
  end

  print("Program ended.")
end
