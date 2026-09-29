// Off-Windows unit test for the JSON serializer (Dump/src/Model.cpp).
//   clang++ -std=c++17 -IDump/include tests/model_test.cpp Dump/src/Model.cpp -o model_test && ./model_test
#include "../Dump/include/Model.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failed = 0;

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        std::string a_ = (actual), e_ = (expected);                                      \
        if (a_ != e_) {                                                                  \
            std::printf("FAIL %s:%d\n  expected: %s\n  actual:   %s\n", __FILE__, __LINE__, \
                        e_.c_str(), a_.c_str());                                         \
            g_failed++;                                                                  \
        }                                                                                \
    } while (0)

static ClassData SampleClass() {
    ClassData c;
    c.name = "PlayerController";
    c.ns = "Game";
    c.kind = "class";
    c.parent = "UnityEngine.MonoBehaviour";
    c.hasToken = true;
    c.token = 0x2000015;
    c.interfaces = { "Game.IDamageable" };

    FieldData f;
    f.name = "health"; f.type = "System.Single"; f.access = "public";
    f.hasOffset = true; f.offset = 0x18;
    c.fields.push_back(f);

    PropertyData p;
    p.name = "IsDead"; p.type = "System.Boolean"; p.access = "public"; p.hasGet = true;
    c.properties.push_back(p);

    MethodData m;
    m.name = "TakeDamage"; m.returns = "System.Void"; m.access = "public";
    m.isVirtual = true; m.hasRva = true; m.rva = 0x1234abc;
    m.params.push_back({ "System.Single", "amount" });
    c.methods.push_back(m);

    MethodData hidden;
    hidden.name = "<Start>b__0"; hidden.returns = "System.Void"; hidden.access = "private";
    c.methods.push_back(hidden);
    return c;
}

static void TestEscape() {
    CHECK_EQ(Json::Escape("a\"b\\c"), "a\\\"b\\\\c");
    CHECK_EQ(Json::Escape("line\nbreak\ttab"), "line\\nbreak\\ttab");
    CHECK_EQ(Json::Escape(std::string("bell\x07", 5)), "bell\\u0007"); // control chars were emitted raw before
    CHECK_EQ(Json::Escape("한글"), "한글");                             // UTF-8 passes through
}

static void TestFullClass() {
    CHECK_EQ(Json::SerializeClass(SampleClass(), false),
        "{\"name\":\"PlayerController\",\"fullName\":\"Game.PlayerController\",\"type\":\"class\","
        "\"token\":\"0x2000015\",\"extends\":\"UnityEngine.MonoBehaviour\",\"implements\":[\"Game.IDamageable\"],"
        "\"fields\":[{\"name\":\"health\",\"type\":\"System.Single\",\"access\":\"public\",\"offset\":\"0x18\"}],"
        "\"properties\":[{\"name\":\"IsDead\",\"type\":\"System.Boolean\",\"access\":\"public\",\"get\":true}],"
        "\"methods\":[{\"name\":\"TakeDamage\",\"returns\":\"System.Void\","
        "\"params\":[{\"type\":\"System.Single\",\"name\":\"amount\"}],\"access\":\"public\",\"virtual\":true,"
        "\"rva\":\"0x1234abc\"},"
        "{\"name\":\"<Start>b__0\",\"returns\":\"System.Void\",\"access\":\"private\"}]}");
}

static void TestSummaryClass() {
    // No token/offset/rva, no "public" access noise, compiler-generated methods dropped.
    CHECK_EQ(Json::SerializeClass(SampleClass(), true),
        "{\"name\":\"PlayerController\",\"fullName\":\"Game.PlayerController\",\"type\":\"class\","
        "\"extends\":\"UnityEngine.MonoBehaviour\",\"implements\":[\"Game.IDamageable\"],"
        "\"fields\":[{\"name\":\"health\",\"type\":\"System.Single\"}],"
        "\"properties\":[{\"name\":\"IsDead\",\"type\":\"System.Boolean\",\"get\":true}],"
        "\"methods\":[{\"name\":\"TakeDamage\",\"returns\":\"System.Void\","
        "\"params\":[{\"type\":\"System.Single\",\"name\":\"amount\"}],\"virtual\":true}]}");
}

static void TestEnumAndStatic() {
    ClassData e;
    e.name = "State"; e.kind = "enum"; e.underlying = "System.Int32";
    FieldData a; a.name = "Idle"; a.type = "Game.State"; a.access = "public";
    a.isStatic = true; a.isConst = true; a.hasValue = true; a.value = "0";
    FieldData b = a; b.name = "Run"; b.value = "-1";
    e.fields = { a, b };
    CHECK_EQ(Json::SerializeClass(e, true),
        "{\"name\":\"State\",\"fullName\":\"State\",\"type\":\"enum\",\"underlying\":\"System.Int32\","
        "\"fields\":[{\"name\":\"Idle\",\"value\":0},{\"name\":\"Run\",\"value\":-1}],\"methods\":[]}");

    ClassData s;
    s.name = "Util"; s.kind = "class"; s.isAbstract = true; s.isSealed = true;
    CHECK_EQ(Json::SerializeClass(s, true),
        "{\"name\":\"Util\",\"fullName\":\"Util\",\"type\":\"class\",\"static\":true,\"fields\":[],\"methods\":[]}");
}

static void TestIndex() {
    IndexEntry e;
    e.assembly = "Assembly-CSharp";
    e.files = { "Assembly_CSharp.json", "Assembly_CSharp.part2.json" };
    e.classCount = 3;
    e.namespaces[""] = 1;
    e.namespaces["Game"] = 2;
    CHECK_EQ(Json::SerializeIndex("il2cpp", "full", { e }),
        "{\"schemaVersion\":2,\"runtime\":\"il2cpp\",\"mode\":\"full\",\"assemblies\":[\n"
        "{\"assembly\":\"Assembly-CSharp\",\"classCount\":3,"
        "\"files\":[\"Assembly_CSharp.json\",\"Assembly_CSharp.part2.json\"],"
        "\"namespaces\":{\"(global)\":1,\"Game\":2}}\n]}\n");
}

int main() {
    TestEscape();
    TestFullClass();
    TestSummaryClass();
    TestEnumAndStatic();
    TestIndex();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all model tests passed\n");
    return 0;
}
