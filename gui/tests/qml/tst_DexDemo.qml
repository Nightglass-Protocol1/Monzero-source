import QtQuick 2.9
import QtTest 1.2
import "../../js/DexDemo.js" as Demo

TestCase {
    name: "DexDemoPrices"
    function test_pairs_data() {
        return [{ tag: "BTC", anchor: 1172500 },
                { tag: "LTC", anchor: 1325 },
                { tag: "BCH", anchor: 6840 }];
    }
    function test_pairs(data) {
        compare(Demo.price(data.anchor, "0.08421"), data.anchor.toFixed(5));
        var asks = [0.08512, 0.08491, 0.08475, 0.08462, 0.08439];
        var bids = [0.08410, 0.08392, 0.08370, 0.08344, 0.08318];
        for (var i = 0; i < asks.length; ++i) {
            verify(Number(Demo.price(data.anchor, asks[i])) > data.anchor);
            verify(Number(Demo.price(data.anchor, bids[i])) < data.anchor);
            if (i) {
                verify(Number(Demo.price(data.anchor, asks[i-1])) > Number(Demo.price(data.anchor, asks[i])));
                verify(Number(Demo.price(data.anchor, bids[i-1])) > Number(Demo.price(data.anchor, bids[i])));
            }
        }
    }
    function test_invalid() {
        compare(Demo.price(0, 0.08421), "—");
        compare(Demo.price(1325, "invalid"), "—");
        compare(Demo.price(Infinity, 0.08421), "—");
    }
}
