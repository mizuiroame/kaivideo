// Trickplay: the server's sheets of small pictures, and which one shows a moment.
#include <string.h>

#include "check.h"
#include "trickplay.h"

static const int64_t S = 1000000000;

void testTrickplay(void)
{
    static const char ITEM[] =
        "{\"Items\":[{\"Id\":\"aaaa1111\",\"Trickplay\":{\"src1\":{"
        "\"160\":{\"Width\":160,\"Height\":90,\"TileWidth\":10,\"TileHeight\":10,\"ThumbnailCount\":300,\"Interval\":10000},"
        "\"320\":{\"Width\":320,\"Height\":180,\"TileWidth\":10,\"TileHeight\":10,\"ThumbnailCount\":135,\"Interval\":10000}},"
        "\"other\":{\"320\":{\"Width\":320,\"Height\":180,\"TileWidth\":5,\"TileHeight\":5,\"ThumbnailCount\":4,\"Interval\":2000}}}}]}";
    TrickplayInfo t;
    CHECK(trickplayParse(ITEM, "src1", &t));
    CHECK(t.width == 320 && t.height == 180 && t.tile_w == 10 && t.tile_h == 10 && t.count == 135 &&
          t.interval_ns == 10 * S);  // The largest up to 320 wide.
    CHECK(trickplayParse(ITEM, "other", &t) && t.tile_w == 5 && t.count == 4);
    CHECK(!trickplayParse(ITEM, "missing", &t));
    CHECK(!trickplayParse("{\"Items\":[{\"Id\":\"x\"}]}", "src1", &t));
    CHECK(!trickplayParse("nope", "src1", &t));

    CHECK(trickplayParse(ITEM, "src1", &t));
    TrickplaySpot spot;
    CHECK(trickplayLocate(&t, 0, &spot) && spot.sheet == 0 && spot.x == 0 && spot.y == 0);
    // 1234 s: picture 123, the 24th of the second sheet: row 2, column 3.
    CHECK(trickplayLocate(&t, 1234 * S, &spot) && spot.sheet == 1 && spot.x == 3 * 320 && spot.y == 2 * 180);
    CHECK(trickplayLocate(&t, 99999 * S, &spot) && spot.sheet == 1 && spot.x == 4 * 320 && spot.y == 3 * 180);  // The last.
    CHECK(trickplayLocate(&t, -5 * S, &spot) && spot.sheet == 0);
    TrickplayInfo empty = {0};
    CHECK(!trickplayLocate(&empty, 0, &spot));
}
