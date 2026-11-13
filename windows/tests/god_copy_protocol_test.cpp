#include "god_copy_protocol.h"
#include <cstdio>
#include <memory>
using namespace cronq::god_copy;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    auto p = std::make_unique<Packet>();
    p->magic = kMagic; p->result = Result::Ok;
    CHECK(!Valid(*p));
    p->length = 1; p->text[0] = L'A';
    CHECK(Valid(*p));
    p->length = kMaxText + 1;
    CHECK(!Valid(*p));
    p->length = 1; p->text[1] = L'x';
    CHECK(!Valid(*p));
    p->text[1] = 0; p->text[0] = 0xD83D;
    CHECK(!Valid(*p));
    p->length = 2; p->text[1] = 0xDE00; p->text[2] = 0;
    CHECK(Valid(*p));
    p->text[0] = 0xDE00;
    CHECK(!Valid(*p));
    p->text[0] = L'A'; p->text[1] = 0;
    CHECK(!Valid(*p));
    p->length = 1; p->result = Result::Pending;
    CHECK(!Valid(*p));
    p->result = Result::Ok; p->magic = 0;
    CHECK(!Valid(*p));
    std::puts("god_copy_protocol_test: all checks passed");
}
