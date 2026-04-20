var math = require("./math_module");
if (math.sum(20, 22) !== 42) {
    throw new Error("require(./math_module) did not return expected exports");
}

var jsonByExtension = require("./a.json");
if (jsonByExtension.name !== "chakra" || jsonByExtension.value !== 42) {
    throw new Error("require(./a.json) failed");
}

var jsonByComma = require("./a,json");
if (jsonByComma.kind !== "json") {
    throw new Error("require(./a,json) failed");
}

var again = require("./math_module");
if (again !== math) {
    throw new Error("require cache did not return identical module exports object");
}

print("require_node_style.js passed");
