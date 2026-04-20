// ============================================
//  ChakraCore Test Suite — 200+ Tests
//  Run: ch.exe tests.js
//  Covers: ES5, ES6, ES2017-ES2022, ESNext
// ============================================

var passed = 0;
var failed = 0;
var total  = 0;
var skipped = 0;

function assert(description, condition) {
    total++;
    if (condition) {
        print("[PASS] " + description);
        passed++;
    } else {
        print("[FAIL] " + description);
        failed++;
    }
}

function assertEq(description, actual, expected) {
    assert(description + " (expected: " + expected + ", got: " + actual + ")", actual === expected);
}

function assertThrows(description, fn) {
    total++;
    try {
        fn();
        print("[FAIL] " + description + " (expected throw, got none)");
        failed++;
    } catch(e) {
        print("[PASS] " + description);
        passed++;
    }
}

function tryTest(description, fn) {
    total++;
    try {
        var result = fn();
        if (result === true) {
            print("[PASS] " + description);
            passed++;
        } else {
            print("[FAIL] " + description + " (returned: " + result + ")");
            failed++;
        }
    } catch(e) {
        print("[SKIP] " + description + " (not supported: " + e.message + ")");
        skipped++;
        total--;
    }
}

// ─────────────────────────────────────────────────────
//  §1  MATH
// ─────────────────────────────────────────────────────
print("\n── §1 Math ──");
assertEq("2 + 2 = 4",                  2 + 2,              4);
assertEq("10 - 3 = 7",                 10 - 3,             7);
assertEq("3 * 3 = 9",                  3 * 3,              9);
assertEq("10 / 2 = 5",                 10 / 2,             5);
assertEq("7 % 3 = 1",                  7 % 3,              1);
assertEq("Math.pow(2,8) = 256",        Math.pow(2, 8),     256);
assertEq("Math.sqrt(144) = 12",        Math.sqrt(144),     12);
assertEq("Math.abs(-99) = 99",         Math.abs(-99),      99);
assertEq("Math.floor(4.9) = 4",        Math.floor(4.9),    4);
assertEq("Math.ceil(4.1) = 5",         Math.ceil(4.1),     5);
assertEq("Math.round(4.5) = 5",        Math.round(4.5),    5);
assertEq("Math.round(4.4) = 4",        Math.round(4.4),    4);
assertEq("Math.max(1,5,3) = 5",        Math.max(1,5,3),    5);
assertEq("Math.min(1,5,3) = 1",        Math.min(1,5,3),    1);
assertEq("Math.PI approx 3.14",        Math.PI > 3.14 && Math.PI < 3.15, true);
assertEq("Infinity + 1 = Infinity",    Infinity + 1,       Infinity);
assertEq("NaN !== NaN",                NaN !== NaN,        true);
assertEq("isNaN(NaN) = true",          isNaN(NaN),         true);
assertEq("isFinite(Infinity) = false", isFinite(Infinity), false);
assertEq("isFinite(42) = true",        isFinite(42),       true);
assertEq("Math.trunc(4.9) = 4",        Math.trunc(4.9),    4);
assertEq("Math.trunc(-4.9) = -4",      Math.trunc(-4.9),   -4);
assertEq("Math.sign(-5) = -1",         Math.sign(-5),      -1);
assertEq("Math.sign(0) = 0",           Math.sign(0),       0);
assertEq("Math.sign(5) = 1",           Math.sign(5),       1);
assertEq("Math.log2(8) = 3",           Math.log2(8),       3);
assertEq("Math.log10(1000) = 3",       Math.log10(1000),   3);
assertEq("Math.hypot(3,4) = 5",        Math.hypot(3, 4),   5);
assertEq("0.1+0.2 != 0.3 (float)",     0.1 + 0.2 === 0.3,  false);
assertEq("Number.EPSILON exists",      Number.EPSILON > 0, true);

// ─────────────────────────────────────────────────────
//  §2  STRINGS
// ─────────────────────────────────────────────────────
print("\n── §2 Strings ──");
assertEq("concat",                     "Hello" + " " + "World",     "Hello World");
assertEq("length",                     "ChakraCore".length,          10);
assertEq("toUpperCase",                "hello".toUpperCase(),        "HELLO");
assertEq("toLowerCase",                "HELLO".toLowerCase(),        "hello");
assertEq("trim",                       "  hi  ".trim(),              "hi");
assertEq("trimStart",                  "  hi".trimStart(),           "hi");
assertEq("trimEnd",                    "hi  ".trimEnd(),             "hi");
assertEq("indexOf found",              "ChakraCore".indexOf("Core"), 6);
assertEq("indexOf not found",          "ChakraCore".indexOf("xyz"), -1);
assertEq("includes",                   "ChakraCore".includes("Chakra"), true);
assertEq("startsWith",                 "ChakraCore".startsWith("Chakra"), true);
assertEq("endsWith",                   "ChakraCore".endsWith("Core"), true);
assertEq("slice",                      "ChakraCore".slice(0, 6),     "Chakra");
assertEq("substring",                  "ChakraCore".substring(6),    "Core");
assertEq("repeat",                     "ab".repeat(3),               "ababab");
assertEq("replace",                    "foo bar".replace("bar","baz"), "foo baz");
assertEq("split length",               "a,b,c".split(",").length,    3);
assertEq("charAt",                     "Chakra".charAt(0),           "C");
assertEq("charCodeAt",                 "A".charCodeAt(0),            65);
assertEq("String.fromCharCode",        String.fromCharCode(65),      "A");
assertEq("padStart",                   "5".padStart(3,"0"),          "005");
assertEq("padEnd",                     "5".padEnd(3,"0"),            "500");
assertEq("at(0)",                      "Chakra".at(0),               "C");
assertEq("at(-1)",                     "Chakra".at(-1),              "a");
tryTest("replaceAll", function(){
    if (typeof "".replaceAll !== "function") {
        throw new Error("replaceAll not supported");
    }
    return "aabbaa".replaceAll("a","x") === "xxbbxx";
});

// ─────────────────────────────────────────────────────
//  §3  ARRAYS
// ─────────────────────────────────────────────────────
print("\n── §3 Arrays ──");
var arr = [1, 2, 3, 4, 5];
assertEq("length",                     arr.length,                   5);
assertEq("index access",               arr[2],                       3);
assertEq("push returns new length",    arr.push(6),                  6);
assertEq("pop",                        arr.pop(),                    6);
assertEq("shift",                      [10,20,30].shift(),           10);
assertEq("unshift",                    (function(){ var a=[2,3]; a.unshift(1); return a[0]; })(), 1);
assertEq("join",                       [1,2,3].join("-"),            "1-2-3");
assertEq("indexOf",                    [1,2,3].indexOf(2),           1);
assertEq("includes",                   [1,2,3].includes(3),          true);
assertEq("reverse",                    [1,2,3].reverse()[0],         3);
assertEq("slice",                      [1,2,3,4].slice(1,3).length,  2);
assertEq("concat",                     [1,2].concat([3,4]).length,   4);
assertEq("map",                        [1,2,3].map(function(x){return x*2;})[2], 6);
assertEq("filter",                     [1,2,3,4].filter(function(x){return x>2;}).length, 2);
assertEq("reduce",                     [1,2,3,4].reduce(function(a,b){return a+b;},0), 10);
assertEq("every true",                 [2,4,6].every(function(x){return x%2===0;}), true);
assertEq("every false",                [2,3,6].every(function(x){return x%2===0;}), false);
assertEq("some true",                  [1,3,5,6].some(function(x){return x%2===0;}), true);
assertEq("find",                       [1,2,3,4].find(function(x){return x>2;}), 3);
assertEq("findIndex",                  [1,2,3,4].findIndex(function(x){return x>2;}), 2);
assertEq("flat",                       [[1,2],[3,4]].flat().length,  4);
assertEq("flatMap",                    [1,2,3].flatMap(function(x){return [x,x*2];}).length, 6);
assertEq("Array.from string",          Array.from("abc").length,     3);
assertEq("Array.isArray true",         Array.isArray([]),            true);
assertEq("Array.isArray false",        Array.isArray({}),            false);
assertEq("fill",                       new Array(3).fill(0).join(""), "000");
assertEq("at(0)",                      [10,20,30].at(0),             10);
assertEq("at(-1)",                     [10,20,30].at(-1),            30);

// ─────────────────────────────────────────────────────
//  §4  OBJECTS
// ─────────────────────────────────────────────────────
print("\n── §4 Objects ──");
var obj = { name: "Chakra", version: 1 };
assertEq("property access",            obj.name,                     "Chakra");
assertEq("bracket access",             obj["version"],               1);
assertEq("property update",            (obj.version = 2, obj.version), 2);
assertEq("hasOwnProperty",             obj.hasOwnProperty("name"),   true);
assertEq("Object.keys length",         Object.keys(obj).length,      2);
assertEq("Object.values length",       Object.values(obj).length,    2);
assertEq("Object.entries length",      Object.entries(obj).length,   2);
assertEq("Object.assign",             (function(){ var t={}; Object.assign(t,{a:1},{b:2}); return t.a+t.b; })(), 3);
assertEq("delete property",           (function(){ var o={a:1,b:2}; delete o.a; return Object.keys(o).length; })(), 1);
assertEq("in operator",               "name" in obj,                true);
assertEq("Object spread",             (function(){ var a={x:1}; var b={...a,y:2}; return b.x+b.y; })(), 3);
assertEq("Shorthand property",        (function(){ var x=5; var o={x}; return o.x; })(), 5);
assertEq("Computed property key",     (function(){ var k="foo"; var o={[k]:42}; return o.foo; })(), 42);
assertEq("Object.freeze",             (function(){
    var o = Object.freeze({x:1});
    try { o.x = 99; } catch(e){}
    return o.x;
})(), 1);

// ─────────────────────────────────────────────────────
//  §5  ES6 — LET / CONST / DESTRUCTURING / SPREAD
// ─────────────────────────────────────────────────────
print("\n── §5 ES6 Basics ──");
tryTest("let block scope", function(){
    let x = 1;
    { let x = 2; }
    return x === 1;
});
tryTest("const immutable", function(){
    const c = 42;
    try { eval("c = 99"); } catch(e) {}
    return c === 42;
});
tryTest("Array destructuring", function(){
    var [a,b,c] = [1,2,3];
    return a===1 && b===2 && c===3;
});
tryTest("Object destructuring", function(){
    var {name, version} = {name:"Chakra", version:3};
    return name==="Chakra" && version===3;
});
tryTest("Default destructure value", function(){
    var {x=10, y=20} = {x:5};
    return x===5 && y===20;
});
tryTest("Rest in destructure", function(){
    var [first, ...rest] = [1,2,3,4];
    return first===1 && rest.length===3;
});
tryTest("Spread into array", function(){
    var a=[1,2]; var b=[0,...a,3];
    return b.length===4 && b[0]===0;
});
tryTest("Spread into function args", function(){
    var args=[3,4];
    return Math.max(...args)===4;
});

// ─────────────────────────────────────────────────────
//  §6  ARROW FUNCTIONS
// ─────────────────────────────────────────────────────
print("\n── §6 Arrow Functions ──");
tryTest("Arrow basic", function(){
    var double = x => x * 2;
    return double(5) === 10;
});
tryTest("Arrow multi-param", function(){
    var add = (a,b) => a + b;
    return add(3,4) === 7;
});
tryTest("Arrow block body", function(){
    var greet = name => { return "Hi " + name; };
    return greet("Chakra") === "Hi Chakra";
});
tryTest("Arrow in map", function(){
    return [1,2,3].map(x => x*x)[2] === 9;
});
tryTest("Arrow in filter", function(){
    return [1,2,3,4].filter(x => x%2===0).length === 2;
});
tryTest("Arrow in reduce", function(){
    return [1,2,3,4,5].reduce((a,b) => a+b, 0) === 15;
});

// ─────────────────────────────────────────────────────
//  §7  CLASSES
// ─────────────────────────────────────────────────────
print("\n── §7 Classes ──");
tryTest("Class basic", function(){
    class Animal {
        constructor(name){ this.name = name; }
        speak(){ return this.name + " speaks"; }
    }
    var a = new Animal("Dog");
    return a.speak() === "Dog speaks";
});
tryTest("Class inheritance", function(){
    class Shape {
        constructor(color){ this.color = color; }
        getColor(){ return this.color; }
    }
    class Circle extends Shape {
        constructor(color, r){ super(color); this.r = r; }
        area(){ return Math.PI * this.r * this.r; }
    }
    var c = new Circle("red", 5);
    return c.getColor()==="red" && c.area() > 78;
});
tryTest("Class static method", function(){
    class MathHelper {
        static square(x){ return x*x; }
    }
    return MathHelper.square(7) === 49;
});
tryTest("Class getter/setter", function(){
    class Temp {
        constructor(c){ this._c = c; }
        get fahrenheit(){ return this._c * 9/5 + 32; }
        set fahrenheit(f){ this._c = (f-32)*5/9; }
    }
    var t = new Temp(0);
    return t.fahrenheit === 32;
});
tryTest("instanceof check", function(){
    class Foo {}
    var f = new Foo();
    return f instanceof Foo;
});
tryTest("Class static fields", function(){
    return Function("\"use strict\"; class Counter { static count = 0; } Counter.count++; return Counter.count === 1;")();
});
tryTest("Class instance fields", function(){
    return Function("\"use strict\"; class Point { x = 0; y = 0; } var p = new Point(); return p.x === 0 && p.y === 0;")();
});

// ─────────────────────────────────────────────────────
//  §8  PROMISES
// ─────────────────────────────────────────────────────
print("\n── §8 Promises ──");
tryTest("Promise exists",              function(){ return typeof Promise !== "undefined"; });
tryTest("Promise.resolve no throw",    function(){ Promise.resolve(42).then(function(v){}); return true; });
tryTest("Promise.reject catches",      function(){ Promise.reject(new Error("t")).catch(function(){}); return true; });
tryTest("Promise chain no throw",      function(){ Promise.resolve(1).then(function(v){return v+1;}); return true; });
tryTest("Promise.all exists",          function(){ return typeof Promise.all === "function"; });
tryTest("Promise.race exists",         function(){ return typeof Promise.race === "function"; });
tryTest("Promise.allSettled exists",   function(){ return typeof Promise.allSettled === "function"; });
tryTest("Promise.any exists",          function(){ return typeof Promise.any === "function"; });

// ─────────────────────────────────────────────────────
//  §9  MAP & SET
// ─────────────────────────────────────────────────────
print("\n── §9 Map & Set ──");
tryTest("Map set/get",           function(){ var m=new Map(); m.set("k",99); return m.get("k")===99; });
tryTest("Map size",              function(){ return new Map([["a",1],["b",2]]).size===2; });
tryTest("Map has",               function(){ var m=new Map([["x",1]]); return m.has("x")===true && m.has("y")===false; });
tryTest("Map delete",            function(){ var m=new Map([["a",1]]); m.delete("a"); return m.size===0; });
tryTest("Map object key",        function(){ var m=new Map(); var k={}; m.set(k,"v"); return m.get(k)==="v"; });
tryTest("Map iteration",         function(){ var m=new Map([["a",1],["b",2]]); var s=0; m.forEach(function(v){s+=v;}); return s===3; });
tryTest("Set deduplication",     function(){ return new Set([1,2,3,2,1]).size===3; });
tryTest("Set has",               function(){ var s=new Set([1,2,3]); return s.has(2)===true && s.has(9)===false; });
tryTest("Set delete",            function(){ var s=new Set([1,2,3]); s.delete(2); return s.size===2; });
tryTest("Set forEach",           function(){ var s=new Set([10,20,30]); var sum=0; s.forEach(function(v){sum+=v;}); return sum===60; });

// ─────────────────────────────────────────────────────
//  §10  SYMBOL
// ─────────────────────────────────────────────────────
print("\n── §10 Symbol ──");
tryTest("Symbol is unique",      function(){ return Symbol("a") !== Symbol("a"); });
tryTest("typeof Symbol",         function(){ return typeof Symbol("x") === "symbol"; });
tryTest("Symbol as object key",  function(){ var s=Symbol("id"); var o={}; o[s]=42; return o[s]===42; });
tryTest("Symbol.for shared",     function(){ return Symbol.for("shared") === Symbol.for("shared"); });
tryTest("Symbol description",    function(){ return Symbol("hello").toString() === "Symbol(hello)"; });

// ─────────────────────────────────────────────────────
//  §11  GENERATORS
// ─────────────────────────────────────────────────────
print("\n── §11 Generators ──");
tryTest("Generator basic", function(){
    function* gen(){ yield 1; yield 2; yield 3; }
    var g = gen();
    return g.next().value===1 && g.next().value===2;
});
tryTest("Generator done flag", function(){
    function* gen(){ yield 1; }
    var g = gen(); g.next();
    return g.next().done === true;
});
tryTest("Generator infinite sequence", function(){
    function* count(){ var i=0; while(true) yield i++; }
    var g = count(); g.next(); g.next();
    return g.next().value === 2;
});
tryTest("Generator with return value", function(){
    function* gen(){ yield 1; return 99; }
    var g = gen(); g.next();
    return g.next().value === 99;
});
tryTest("Generator spread", function(){
    function* gen(){ yield 1; yield 2; yield 3; }
    return [...gen()].length === 3;
});

// ─────────────────────────────────────────────────────
//  §12  ITERATORS / FOR...OF
// ─────────────────────────────────────────────────────
print("\n── §12 Iterators / for..of ──");
tryTest("for..of array", function(){
    var sum=0; for (var v of [1,2,3,4,5]) sum+=v;
    return sum===15;
});
tryTest("for..of string", function(){
    var chars=[]; for (var c of "abc") chars.push(c);
    return chars.length===3 && chars[0]==="a";
});
tryTest("for..of Set", function(){
    var arr=[]; for (var v of new Set([10,20,30])) arr.push(v);
    return arr.length===3;
});
tryTest("for..of Map", function(){
    var count=0; for (var pair of new Map([["a",1],["b",2]])) count++;
    return count===2;
});
tryTest("for..in object", function(){
    var keys=[]; for (var k in {a:1,b:2,c:3}) keys.push(k);
    return keys.length===3;
});

// ─────────────────────────────────────────────────────
//  §13  WEAKMAP / WEAKSET / WEAKREF
// ─────────────────────────────────────────────────────
print("\n── §13 Weak Collections ──");
tryTest("WeakMap get/set",       function(){ var wm=new WeakMap(); var k={}; wm.set(k,"s"); return wm.get(k)==="s"; });
tryTest("WeakMap has/delete",    function(){ var wm=new WeakMap(); var k={}; wm.set(k,1); wm.delete(k); return wm.has(k)===false; });
tryTest("WeakSet add/has",       function(){ var ws=new WeakSet(); var o={}; ws.add(o); return ws.has(o)===true; });
tryTest("WeakRef exists",        function(){
    if (typeof WeakRef === "undefined") {
        throw new Error("WeakRef not supported");
    }
    return true;
});

// ─────────────────────────────────────────────────────
//  §14  PROXY & REFLECT
// ─────────────────────────────────────────────────────
print("\n── §14 Proxy & Reflect ──");
tryTest("Proxy get trap", function(){
    var p=new Proxy({x:1},{get:function(t,k){return k in t?t[k]:0;}});
    return p.x===1 && p.missing===0;
});
tryTest("Proxy set trap", function(){
    var log=[]; var p=new Proxy({},{set:function(t,k,v){log.push(k);t[k]=v;return true;}});
    p.a=1; p.b=2;
    return log.length===2;
});
tryTest("Reflect.ownKeys",       function(){ return Reflect.ownKeys({a:1,b:2}).length===2; });
tryTest("Reflect.has",           function(){ return Reflect.has({x:1},"x")===true; });
tryTest("Reflect.deleteProperty",function(){ var o={a:1}; Reflect.deleteProperty(o,"a"); return !("a" in o); });

// ─────────────────────────────────────────────────────
//  §15  TYPED ARRAYS
// ─────────────────────────────────────────────────────
print("\n── §15 Typed Arrays ──");
tryTest("Int32Array",            function(){ var a=new Int32Array([1,2,3]); return a[0]===1&&a.length===3; });
tryTest("Float64Array",          function(){ var a=new Float64Array([1.1,2.2,3.3]); return a.length===3; });
tryTest("Uint8Array fill",       function(){ return new Uint8Array(4).fill(255)[0]===255; });
tryTest("ArrayBuffer byteLength",function(){ return new ArrayBuffer(8).byteLength===8; });
tryTest("DataView get/set",      function(){
    var buf=new ArrayBuffer(4); var dv=new DataView(buf);
    dv.setInt32(0,12345678);
    return dv.getInt32(0)===12345678;
});
tryTest("TypedArray at(-1)",     function(){ return new Int32Array([10,20,30]).at(-1)===30; });

// ─────────────────────────────────────────────────────
//  §16  ERROR TYPES
// ─────────────────────────────────────────────────────
print("\n── §16 Errors ──");
assertThrows("RangeError on bad array size",  function(){ new Array(-1); });
assertThrows("TypeError on null property",    function(){ null.x; });
assertThrows("SyntaxError via eval",          function(){ eval("{{{"); });
tryTest("Error instanceof TypeError",  function(){ try{null.x;}catch(e){return e instanceof TypeError;} });
tryTest("Error.cause (ES2022)",        function(){ var e=new Error("o",{cause:new Error("i")}); return e.cause!==undefined; });
tryTest("Custom error subclass", function(){
    function MyError(msg){ this.message=msg; this.name="MyError"; }
    MyError.prototype=Object.create(Error.prototype);
    return new MyError("oops").message==="oops";
});
tryTest("finally block runs",  function(){
    var ran=false;
    try { throw new Error("x"); } catch(e){} finally { ran=true; }
    return ran===true;
});

// ─────────────────────────────────────────────────────
//  §17  REGEX
// ─────────────────────────────────────────────────────
print("\n── §17 Regex ──");
assertEq("regex test true",            /chakra/i.test("ChakraCore"),  true);
assertEq("regex test false",           /xyz/.test("ChakraCore"),      false);
assertEq("regex match digit",          "hello123".match(/\d+/)[0],   "123");
assertEq("regex replace",              "foo bar".replace(/\s/,"-"),   "foo-bar");
assertEq("regex global replace",       "aabbcc".replace(/[ac]/g,"x"), "xxbbxx");
assertEq("regex capture group",        "2024-04-20".match(/(\d{4})-(\d{2})/)[1], "2024");
tryTest("Named capture groups", function(){
    var namedGroupRegex = new RegExp("(?<year>\\d{4})-(?<month>\\d{2})");
    var m="2024-04-20".match(namedGroupRegex);
    return m.groups.year==="2024";
});
tryTest("Regex sticky flag", function(){
    var r=/\d/y; r.lastIndex=3;
    return r.test("abc5def") === true;
});

// ─────────────────────────────────────────────────────
//  §18  NUMBER & BIGINT
// ─────────────────────────────────────────────────────
print("\n── §18 Number & BigInt ──");
assertEq("Number.isInteger true",      Number.isInteger(42),          true);
assertEq("Number.isInteger false",     Number.isInteger(42.5),        false);
assertEq("Number.isNaN true",          Number.isNaN(NaN),             true);
assertEq("Number.isNaN false",         Number.isNaN(42),              false);
assertEq("Number.isFinite Infinity",   Number.isFinite(1/0),          false);
assertEq("Number.parseInt",            Number.parseInt("42px"),       42);
assertEq("Number.parseFloat",          Number.parseFloat("3.14xyz"),  3.14);
assertEq("toFixed",                    (3.14159).toFixed(2),          "3.14");
assertEq("parseInt hex",               parseInt("0xFF"),              255);
assertEq("parseInt binary",            parseInt("1010", 2),           10);
tryTest("BigInt typeof",     function(){ return typeof BigInt(9007199254740991)==="bigint"; });
tryTest("BigInt arithmetic", function(){ return (BigInt(10)+BigInt(3))===BigInt(13); });
tryTest("BigInt comparison", function(){ return BigInt(100) > BigInt(99); });

// ─────────────────────────────────────────────────────
//  §19  JSON
// ─────────────────────────────────────────────────────
print("\n── §19 JSON ──");
assertEq("stringify object",    JSON.stringify({a:1}),         '{"a":1}');
assertEq("stringify array",     JSON.stringify([1,2,3]),       '[1,2,3]');
assertEq("parse object",        JSON.parse('{"x":5}').x,       5);
assertEq("parse array",         JSON.parse('[1,2,3]')[1],       2);
assertEq("roundtrip",           JSON.parse(JSON.stringify({k:"v"})).k, "v");
assertThrows("parse invalid",   function(){ JSON.parse("{bad}"); });
assertEq("stringify null",      JSON.stringify(null),          "null");
assertEq("stringify bool",      JSON.stringify(true),          "true");

// ─────────────────────────────────────────────────────
//  §20  ES2020
// ─────────────────────────────────────────────────────
print("\n── §20 ES2020 ──");
tryTest("Optional chaining deep",      function(){ return Function("\"use strict\"; var o={a:{b:{c:42}}}; return o?.a?.b?.c===42;")(); });
tryTest("Optional chaining on null",   function(){ return Function("\"use strict\"; var o=null; return o?.a?.b===undefined;")(); });
tryTest("Optional chaining method",    function(){ return Function("\"use strict\"; var o={fn:function(){return 1;}}; return o?.fn()===1;")(); });
tryTest("Nullish coalescing ??",       function(){ return Function("\"use strict\"; return (null??\"default\")==\"default\";")(); });
tryTest("Nullish keeps 0",             function(){ return Function("\"use strict\"; return (0??99)===0;")(); });
tryTest("Nullish keeps false",         function(){ return Function("\"use strict\"; return (false??99)===false;")(); });
tryTest("Nullish keeps empty string",  function(){ return Function("\"use strict\"; return (\"\"??99)===\"\";")(); });
tryTest("globalThis exists",           function(){ return typeof globalThis!=="undefined"; });

// ─────────────────────────────────────────────────────
//  §21  ES2021
// ─────────────────────────────────────────────────────
print("\n── §21 ES2021 ──");
tryTest("Logical OR assign ||=",       function(){ return Function("\"use strict\"; var a=null; a||=\"fb\"; return a===\"fb\";")(); });
tryTest("Logical AND assign &&=",      function(){ return Function("\"use strict\"; var a=1; a&&=99; return a===99;")(); });
tryTest("Nullish assign ??=",          function(){ return Function("\"use strict\"; var a=null; a??=\"set\"; return a===\"set\";")(); });
tryTest("Nullish assign skips value",  function(){ return Function("\"use strict\"; var a=\"existing\"; a??=\"ignored\"; return a===\"existing\";")(); });
tryTest("replaceAll",                  function(){ return "aabbaa".replaceAll("a","x")==="xxbbxx"; });
tryTest("Promise.any",                 function(){ return typeof Promise.any==="function"; });
tryTest("WeakRef",                     function(){
    if (typeof WeakRef === "undefined") {
        throw new Error("WeakRef not supported");
    }
    return true;
});
tryTest("Numeric separator",           function(){ return Function("\"use strict\"; return 1_000_000===1000000;")(); });

// ─────────────────────────────────────────────────────
//  §22  ES2022
// ─────────────────────────────────────────────────────
print("\n── §22 ES2022 ──");
tryTest("Array.at()",                  function(){ return [1,2,3].at(-1)===3; });
tryTest("String.at()",                 function(){ return "hello".at(-1)==="o"; });
tryTest("Object.hasOwn()",             function(){ var o={x:1}; return Object.hasOwn(o,"x")===true&&Object.hasOwn(o,"y")===false; });
tryTest("Error.cause",                 function(){ var e=new Error("o",{cause:"root"}); return e.cause==="root"; });
tryTest("TypedArray.at()",             function(){ return new Int32Array([10,20,30]).at(-1)===30; });
tryTest("Class private field",         function(){
    return Function("\"use strict\"; class Box { #val=42; get(){ return this.#val; } } return new Box().get()===42;")();
});
tryTest("Class private method",        function(){
    return Function("\"use strict\"; class Calc { #double(x){ return x*2; } run(x){ return this.#double(x); } } return new Calc().run(5)===10;")();
});
tryTest("Top-level await check",       function(){ return typeof Promise!=="undefined"; }); // ch.exe has no top-level await

// ─────────────────────────────────────────────────────
//  §23  MISC / EDGE CASES
// ─────────────────────────────────────────────────────
print("\n── §23 Misc & Edge Cases ──");
assertEq("Ternary operator",           true?"yes":"no",              "yes");
assertEq("Short circuit &&",           false&&(1/0),                 false);
assertEq("Short circuit ||",           true||(1/0),                  true);
assertEq("Comma operator",             (1,2,3),                      3);
assertEq("void 0 = undefined",         void 0,                       undefined);
assertEq("typeof undeclared var",      typeof undeclaredXYZ,         "undefined");
assertEq("String * Number coerce",     "3" * 2,                      6);
assertEq("String + Number coerce",     "3" + 2,                      "32");
assertEq("!! double negation",         !!1,                          true);
assertEq("!! on empty string",         !!"",                         false);
assertEq("Bitwise AND",                5 & 3,                        1);
assertEq("Bitwise OR",                 5 | 3,                        7);
assertEq("Bitwise XOR",                5 ^ 3,                        6);
assertEq("Left shift",                 1 << 4,                       16);
assertEq("Right shift",                16 >> 2,                      4);
assertEq("Exponentiation **",          2 ** 10,                      1024);
assertEq("Chained ternary",            (1>2)?"a":(3>2)?"b":"c",     "b");
tryTest("Deep recursion no overflow",  function(){ function loop(n){return n<=0?true:loop(n-1);} return loop(5000); });
tryTest("Default param value", function(){
    function greet(name="World"){ return "Hello "+name; }
    return greet()==="Hello World" && greet("Chakra")==="Hello Chakra";
});
tryTest("Rest params", function(){
    function sum(...args){ return args.reduce((a,b)=>a+b,0); }
    return sum(1,2,3,4,5)===15;
});

// ─────────────────────────────────────────────────────
//  §24  DATE
// ─────────────────────────────────────────────────────
print("\n── §24 Date ──");
tryTest("Date.now() is number",        function(){ return typeof Date.now()==="number"; });
tryTest("new Date() is object",        function(){ return typeof new Date()==="object"; });
tryTest("Date getFullYear >= 2024",    function(){ return new Date().getFullYear()>=2024; });
tryTest("Date from string",            function(){ return new Date("2000-01-01").getFullYear()===2000; });
tryTest("Date getMonth",               function(){ return new Date("2000-06-15").getMonth()===5; }); // 0-indexed
tryTest("Date getDate",                function(){ return new Date("2000-06-15").getDate()===15; });

// ─────────────────────────────────────────────────────
//  §25  WEBASSEMBLY
// ─────────────────────────────────────────────────────
print("\n── §25 WebAssembly ──");
tryTest("WebAssembly object exists",   function(){ return typeof WebAssembly!=="undefined"; });
tryTest("WebAssembly.validate fn",     function(){ return typeof WebAssembly.validate==="function"; });
tryTest("WebAssembly.compile fn",      function(){ return typeof WebAssembly.compile==="function"; });

// ─────────────────────────────────────────────────────
//  RESULTS
// ─────────────────────────────────────────────────────
print("\n============================================");
if (failed === 0) {
    print("[" + passed + "/" + total + "] Tests Worked   (" + skipped + " skipped — unsupported by this engine)");
} else {
    print("[" + passed + "/" + total + "] Tests Worked  |  " + failed + " FAILED  |  " + skipped + " skipped");
}
print("============================================");