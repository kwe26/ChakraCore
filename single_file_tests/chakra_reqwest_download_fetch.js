var reqwest = require("chakra:reqwest");
var fs = require("chakra:fs");

var sourceUrl = "http://127.0.0.1:8765/single_file_tests/download_source.txt";
var outputPath = "./single_file_tests/download_out.txt";

var downloaded = reqwest.downloadFetch(sourceUrl, outputPath, 4);
if (downloaded !== true) {
    throw new Error("chakra:reqwest.downloadFetch did not return true");
}

if (!fs.existsSync(outputPath)) {
    throw new Error("chakra:reqwest.downloadFetch did not create the output file");
}

var content = fs.readFileSync(outputPath);
if (content.indexOf("parallel-download-smoke") < 0) {
    throw new Error("downloaded file content is unexpected");
}

print("chakra_reqwest_download_fetch.js passed");
