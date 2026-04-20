var es2020 = require("chakra:es2020");
var es2021 = require("chakra:es2021");

if (typeof es2020.analyze !== "function") {
    throw new Error("chakra:es2020.analyze is unavailable");
}

if (typeof es2021.analyze !== "function") {
    throw new Error("chakra:es2021.analyze is unavailable");
}

var report2020 = es2020.analyze("const out = source?.value ?? 42n;");
if (report2020.parse_ok !== true) {
    throw new Error("chakra:es2020.analyze did not mark parse_ok");
}

if (!report2020.optional_chaining || !report2020.nullish_coalescing || !report2020.bigint_literals) {
    throw new Error("chakra:es2020.analyze did not detect expected ES2020 features");
}

var report2021 = es2021.analyze("let a = 0, b = 1, c = 2; a &&= b; b ||= c; c ??= a;");
if (report2021.parse_ok !== true) {
    throw new Error("chakra:es2021.analyze did not mark parse_ok");
}

if (!report2021.logical_and_assignment || !report2021.logical_or_assignment || !report2021.logical_nullish_assignment) {
    throw new Error("chakra:es2021.analyze did not detect expected ES2021 assignment features");
}

var parseFailed = false;
try {
    es2020.analyze("const = broken ???");
} catch (error) {
    parseFailed = true;
}

if (!parseFailed) {
    throw new Error("chakra:es2020.analyze should reject invalid source");
}

print("chakra_es_modules.js passed");
