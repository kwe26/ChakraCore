var invoiceTotal = 12_500;
var paid = 2_750;
var remaining = invoiceTotal - paid;

var flagA = 0;
var flagB = 4;
var fallback = null;

flagA ||= 9;
flagB &&= 2;
fallback ??= 11;

if (remaining !== 9750) {
    throw new Error("Numeric separators failed in this runtime");
}

if (flagA !== 9 || flagB !== 2 || fallback !== 11) {
    throw new Error("Logical assignment operators failed in this runtime");
}

console.log("remaining=" + remaining);
console.log("logical-assignments=" + flagA + "," + flagB + "," + fallback);

if (typeof Promise.any !== "function") {
    throw new Error("Promise.any is unavailable in this Chakra build");
}

Promise.any([
    Promise.reject(new Error("first candidate failed")),
    Promise.resolve("winner-from-es2021"),
    Promise.resolve("backup-winner")
]).then(
    function(value) {
        console.log("Promise.any result=" + value);
        console.log("es2021.js passed");
    },
    function(error) {
        throw new Error("Promise.any unexpectedly rejected: " + error);
    }
);
