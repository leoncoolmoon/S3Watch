#include "test.h"
#include "ip_parse.h"

void run_ip_parse_tests(int *fails) {
    int v[4];

    // Happy path
    EXPECT_TRUE(ip_parse_v4("10.10.10.1", v));
    EXPECT_EQ(v[0], 10); EXPECT_EQ(v[1], 10);
    EXPECT_EQ(v[2], 10); EXPECT_EQ(v[3], 1);

    // Edges
    EXPECT_TRUE(ip_parse_v4("0.0.0.0", v));
    EXPECT_EQ(v[0], 0); EXPECT_EQ(v[1], 0);
    EXPECT_EQ(v[2], 0); EXPECT_EQ(v[3], 0);

    EXPECT_TRUE(ip_parse_v4("255.255.255.255", v));
    EXPECT_EQ(v[0], 255); EXPECT_EQ(v[1], 255);
    EXPECT_EQ(v[2], 255); EXPECT_EQ(v[3], 255);

    // Out-of-range octet
    EXPECT_FALSE(ip_parse_v4("256.0.0.0", v));
    EXPECT_FALSE(ip_parse_v4("10.10.10.300", v));
    EXPECT_FALSE(ip_parse_v4("-1.0.0.0", v));

    // Trailing garbage
    EXPECT_FALSE(ip_parse_v4("10.10.10.1x", v));
    EXPECT_FALSE(ip_parse_v4("10.10.10.1.5", v));

    // Wrong number of octets
    EXPECT_FALSE(ip_parse_v4("10.10.10", v));
    EXPECT_FALSE(ip_parse_v4("10.10", v));
    EXPECT_FALSE(ip_parse_v4("10", v));
    EXPECT_FALSE(ip_parse_v4("", v));

    // NULL input
    EXPECT_FALSE(ip_parse_v4(NULL, v));

    // Junk
    EXPECT_FALSE(ip_parse_v4("hello", v));
    EXPECT_FALSE(ip_parse_v4("10.10.x.1", v));
}
