var fs = require("chakra:fs");

if (typeof fs.readFileSync !== "function") {
    throw new Error("chakra:fs.readFileSync is unavailable");
}

if (typeof fs.writeFileSync !== "function") {
    throw new Error("chakra:fs.writeFileSync is unavailable");
}

if (typeof fs.existsSync !== "function") {
    throw new Error("chakra:fs.existsSync is unavailable");
}

if (!fs.existsSync("./single_file_tests/a.json")) {
    throw new Error("chakra:fs.existsSync failed for a known file");
}

var text = fs.readFileSync("./single_file_tests/a.json");
if (text.indexOf("\"name\": \"chakra\"") < 0) {
    throw new Error("chakra:fs.readFileSync failed to read JSON file content");
}

print("chakra_fs_module.js passed");
