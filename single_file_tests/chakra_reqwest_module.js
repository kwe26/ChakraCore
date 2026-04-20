var reqwest = require("chakra:reqwest");

if (typeof reqwest.get !== "function") {
    throw new Error("chakra:reqwest.get is unavailable");
}

if (typeof reqwest.post !== "function") {
    throw new Error("chakra:reqwest.post is unavailable");
}

if (typeof reqwest.fetch !== "function") {
    throw new Error("chakra:reqwest.fetch is unavailable");
}

if (typeof reqwest.downloadFetch !== "function") {
    throw new Error("chakra:reqwest.downloadFetch is unavailable");
}

print("chakra_reqwest_module.js passed");
