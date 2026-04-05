#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace test::tls {

inline constexpr char kCaPem[] =
R"(-----BEGIN CERTIFICATE-----
MIIDBTCCAe2gAwIBAgIUa7FJzM/xwAlSM5mqS6RcHMSJuMgwDQYJKoZIhvcNAQEL
BQAwEjEQMA4GA1UEAwwHVGVzdCBDQTAeFw0yNjA0MDMwODU5MzZaFw0yNzA0MDMw
ODU5MzZaMBIxEDAOBgNVBAMMB1Rlc3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IB
DwAwggEKAoIBAQDCAIZo+01JlLAKG4TdvJwr2zht3u5+zT6lutlUI4BmJKSWjU5G
AK2DEGthXQTyxyHgyAkGDTyJXxbq4eUvQ06OlHcy6nhSrs9ugeB0TK/HTgclsMKs
Sph1BjorazFryZoeZ1pZikCG/nnSG15lBW+oq4qq1BLkNWjohNgcBXNKPnwOj2m+
SH7YOa24NscrAx98nACQ3Tr7V4/BKE7n+TB8ouwXpGOFYVnsDZViyxkjw0rP8Fz2
Px1Vlf1LRDskLHxhkywTlCzdqyNlarZU7nK8hxnNoxnX6eGuOftmmzGRbOCbQ9XD
WDJ3SnLrz0eCt9OSSSCs1EDjVlkQbjjovGYBAgMBAAGjUzBRMB0GA1UdDgQWBBQ1
AT/Du+v0lBaLH4gs7r2twGqHkjAfBgNVHSMEGDAWgBQ1AT/Du+v0lBaLH4gs7r2t
wGqHkjAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQBbTor00ad+
X48pE14CX/vHMAyjtcclaWdbe6S7LfrL9343iubzY1Cc1Q8R785WQpkQq8BbK+dc
cUMNqP4wfqgFj0muJtgya6ySPxcZPE/JEg3b24ZqVftCazeyj+iDRjFeqrore9E3
C3HGLVSoXnhoeH+baXQA50GKQVOx0UwAZe1eUQAYiuq8zGRMcsJnLXHYl7y94Uv+
Bin/2gzUdSFrs8c5WwnOHOR+TL8v+QAIHAyAUKe/YhQ3YaMhLbGD2to89FF+Av03
/xVpKtdT7seWcjCemUUOrgLgLNkifEJliwo5HNcyu2NzG5I07Td6pacrMCbKa/Z/
gdiWBHAVrrpW
-----END CERTIFICATE-----
)";

inline constexpr char kServerCertPem[] =
R"(-----BEGIN CERTIFICATE-----
MIICzDCCAbSgAwIBAgIUH/1yQvecv3epgFP0xA3QIUl4WG0wDQYJKoZIhvcNAQEL
BQAwEjEQMA4GA1UEAwwHVGVzdCBDQTAeFw0yNjA0MDMwODU5MzZaFw0yNzA0MDMw
ODU5MzZaMBQxEjAQBgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBALShgkVADAxfaNjSl7HyM5ZcocyxLAsPr3LjJvtCijFRDJTN
6AkquAXYulTGFelb6z8hG3SE0wW9v2D31CkCw6jiVrR1aXpSTN06Igbzrk8Uz1XU
kcbHNvx7f0tmodjvi05sBeQBGafMHxaA14eiJeXhHd0Jmj8eVdI53MutK707EQeu
6bAtw0Vyvy2lzhm7kHYNBvr8C21NqZ0lPt3FflsYX+DxBygJMkMocd7JE4HV7C2R
FrtpisRW1urnj0igCQx/+O4Gu3u0R0RfRtXJwcXCPDjAzAe26559k6YgFygH8KLB
AcxHaZ5OasxsdE/POlc2z4JICEb9cV1A0n+dePUCAwEAAaMYMBYwFAYDVR0RBA0w
C4IJbG9jYWxob3N0MA0GCSqGSIb3DQEBCwUAA4IBAQA+BPNrJAZl62meFQZ1dTH0
dR3V+9Rw03PBfhZmx/8RjhV16XfwYlXZZ5OiY/aArA3ApclltX+AmMcul06w36mD
cVkpU6oenEc9Rvbu8/bdfREoVAX3LpBV82TT40BO5YmvR8igFfz5bcWB2sA8cRdb
TxdlEesiHuFEabVxUeVunSSdLCowUlV4B7fWO7Z0AvzfzvyXnnzifg1CImWgwlOM
Q3SVRw/lKkJ2+buEkWxN+LXQRbsPQfJVIGbwO4syJ4s0RZWfUhpN+WMFyWuupwQ0
1N/At568SaMrUucsEmT+G9Mhon6XKXVEgzk3Yd0AZHAFlcH3c5oUAizkWAEFJIHt
-----END CERTIFICATE-----
)";

inline constexpr char kServerKeyPem[] =
R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC0oYJFQAwMX2jY
0pex8jOWXKHMsSwLD69y4yb7QooxUQyUzegJKrgF2LpUxhXpW+s/IRt0hNMFvb9g
99QpAsOo4la0dWl6UkzdOiIG865PFM9V1JHGxzb8e39LZqHY74tObAXkARmnzB8W
gNeHoiXl4R3dCZo/HlXSOdzLrSu9OxEHrumwLcNFcr8tpc4Zu5B2DQb6/AttTamd
JT7dxX5bGF/g8QcoCTJDKHHeyROB1ewtkRa7aYrEVtbq549IoAkMf/juBrt7tEdE
X0bVycHFwjw4wMwHtuuefZOmIBcoB/CiwQHMR2meTmrMbHRPzzpXNs+CSAhG/XFd
QNJ/nXj1AgMBAAECggEBALAeegJDfHvj2zrtuvLKEPqevzPx29u9I3iFetCqBEwp
lnbQfVnSyTMKKkPWEC9EbI9VHIvzpj2G0T8e5UJHa7cTWS8C/CFEdaWOtPbKSj7Z
L3+kj66dN0PetjMyksQObGm+cc/qMrWlFPrADWY193W4lYGbh0XbshoUSPBenLMX
aoq1/xJV9Y1wOCTYRpUTerX831d93l4N3WXzT1ZQLxJCrkJWcdcLGHeF+N8KZh6B
ouwHstd4Sy5kd/Lux1ubDDpcR7UY0ngXFa5Pe7ApYAi9LK9SQQMfx1Qy2TfGTI36
9/mHMqxKDmTCrlk8XFgLl9h97WdzhZjqKe/YFD2ed9ECgYEA8EXhQ9nwk98qEVt0
R5e64VrJicHhus83lybyLymnsG9cHauGD71p8bdphPyxdfO7NjhL9lCCMeRDf+XX
JdhsBCgApIuTH8Pc07GpsIumPq1M3boHasKYV5p35GZER29c5dyqDLOLJxOCo6M4
orBv6a1O4bw7bi+jfexC9sNNB/8CgYEAwHQ9bmvDtz8ID0f1tMx6kkFrwTdBS9/a
H+hPmMZ3jhluiPy36wHUoi0LSHZTqBiPyLNhbm5J16uw67fw0T0renqJrGuuDacK
iD1jKYuMdZP1Z0pwTfAzGK/l5re2OztzTJYtmZiABu8LWlXSQIe8UynLcdiy24UL
MUYh8kap3wsCgYA1X3CAwALe9i3EGUqlNKFAuggW38ii1LEGlJIrw9cKLFKMDLQE
/xBvr0xzTCanivLFQpAtMQkayBfua0H1mDO2YKRz6MVPwxRkugx1uS++sIRopJBN
ONjabOfBBq6YJ7a9IN1tYNzCW2UjsHg+O29Au0zQfB2/hjmNGVRuuEd+RQKBgALe
eL/5qUxFC0i3COmuFrGxefrCaR3Jc4YAP5eGdefPZz9xjQha2aGlTELDSNH7s8EJ
M42i5UTq0VNiRZvI7qn/w6enX0vizpxjVeQbqXdjQkhM6smuqARZMvMyj+voIfrl
Tj648EeqLqAlIWJG477Vo6vJ0DjHjfgpFH55ITUrAoGAMEmKkE0nJL7uK5HDLqDJ
PlrVEcGqk4NJtA/9D28UBV/GlrctVlmmLHYRQ50H6FazRoHyuEgqQLEsaZnmhrnk
HqXu7qRHj/fb8aRYrYwdO6/W7iJ7OuF13ZL61hvTLbJHfjqjzJG+V8FU2BuxX8mW
R4vJPwjJqL8AgxyBHbyArRY=
-----END PRIVATE KEY-----
)";

inline constexpr char kWrongCaPem[] =
R"(-----BEGIN CERTIFICATE-----
MIIDETCCAfmgAwIBAgIUGJEzSch7HyjGvjFsP4wdpXdKaxkwDQYJKoZIhvcNAQEL
BQAwGDEWMBQGA1UEAwwNV3JvbmcgVGVzdCBDQTAeFw0yNjA0MDMwODU5MzZaFw0y
NzA0MDMwODU5MzZaMBgxFjAUBgNVBAMMDVdyb25nIFRlc3QgQ0EwggEiMA0GCSqG
SIb3DQEBAQUAA4IBDwAwggEKAoIBAQDFF5lA1EcyxsnNsK9+cPnwINGUZx50Q+dL
ZdZVcWaZwEchnCE4xKN9Ndkhh89cpHAbUtFQ/iuhkmlBFXnV+oZQZ5BYfg5IbZrM
rPUWp2qMCeo6TZj3h8psMzvM+sEGc6wJpav2e0I7l/KgNbDzSIPaCLjB5C+sohJH
2e1/RGYq1gFdTs1ZXM3Ay5joTgH2jEb8n6m8kh9XBE5+XGfTeiBc30CYu/DE2t4I
hd3jaBNAIzGaMDCNbUt9LOcd1HEYR6UnDdTCLiJKsWNaKlM2DMs5SNILcRW22lTK
lUPWdJfl6lNDy+0ANPeLpQGyv1uGZOCMSQqoQJLnTE59/2nyJOtVAgMBAAGjUzBR
MB0GA1UdDgQWBBRShky4i79Nkv+Apg2hbyQzQve76jAfBgNVHSMEGDAWgBRShky4
i79Nkv+Apg2hbyQzQve76jAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUA
A4IBAQDCOl6n8BHUaAXF3spld2RQiKAhMKGl4rW8jY63fFV+WTvxOnMGCpSBK/R5
XQSAPNjxgTJTXuN6uihAc26tdZGemijbajQ416B26UlNYzMDV0zim4VELGhspj9O
ucMe/YeveDesLYQL+0zF421NDgGGzEAAS6dOx6a/+VJwiJVA1iLGqtY0CxkYB46O
cGYz9Y+8blXh0/clc4mZ9X0PWadSe/IiI1+aqWFhOhvrh0g+7Cwf1KJdZzHf555E
YXgRxIWst0R1KccFTeONwOBIPslOjCjQAc/lppBM7yjFbloJ9hCJJMwhTxQXJMyP
15W02eb86VAb7WExZkIPDtsunf8s
-----END CERTIFICATE-----
)";

inline bool WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output << content;
    return output.good();
}

inline bool WriteFixtureSet(const std::filesystem::path& directory) {
    const auto write = [&directory](const std::string& name, const std::string& content) {
        return WriteFile(directory / name, content);
    };

    return write("ca.pem", kCaPem) &&
           write("wrong-ca.pem", kWrongCaPem) &&
           write("server.crt", kServerCertPem) &&
           write("server.key", kServerKeyPem) &&
           write("gateway.crt", kServerCertPem) &&
           write("gateway.key", kServerKeyPem) &&
           write("login_server.crt", kServerCertPem) &&
           write("login_server.key", kServerKeyPem) &&
           write("player_server.crt", kServerCertPem) &&
           write("player_server.key", kServerKeyPem) &&
           write("battle_server.crt", kServerCertPem) &&
           write("battle_server.key", kServerKeyPem);
}

}  // namespace test::tls
