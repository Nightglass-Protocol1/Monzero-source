// Bundles the page (src/app.js) with monero-ts for one network. Run through build.sh.
const path = require("path");
const MONERO_TS = process.env.MONERO_TS;                      // prepared monero-ts checkout (web-wallet/build.sh)
const webpack = require(path.join(MONERO_TS, "node_modules/webpack"));
const base = require(path.join(MONERO_TS, "webpack.base.js"));
module.exports = {
  ...base,
  name: "Monzero web wallet page",
  context: __dirname,
  entry: "./src/app.js",
  devtool: false,                                              // no source maps, no eval
  output: { path: path.resolve(process.env.OUT_DIR), filename: "app.js" },
  module: { rules: [] },                                       // plain modern JavaScript, nothing to transpile
  resolve: {
    ...base.resolve,
    modules: ["node_modules", path.join(__dirname, "node_modules"), path.join(MONERO_TS, "node_modules")],
    alias: {
      ...base.resolve.alias,
      "monero-ts": path.join(MONERO_TS, "dist/index.js"),
      "qrcode-generator": path.join(__dirname, "node_modules/qrcode-generator/dist/qrcode.mjs"),
    },
  },
  plugins: [
    ...base.plugins,
    new webpack.DefinePlugin({ __NETWORK__: JSON.stringify(process.env.NETWORK), __RELEASE__: JSON.stringify(process.env.RELEASE) }),
  ],
  performance: { hints: false },
};
