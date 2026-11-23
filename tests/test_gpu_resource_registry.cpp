// gfx/GpuResourceRegistry without a GL context: GlHandle over fake traits that count their
// Gen/Del calls, and IGpuRestorable owners that record the order they were restored in.
#include <gtest/gtest.h>

#include "gfx/GlHandle.h"
#include "gfx/GpuResourceRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeTraits {
    static inline GLuint next = 100;
    static inline std::vector<GLuint> deleted;
    static GLuint Gen() { return next++; }
    static void Del(GLuint id) { deleted.push_back(id); }
};
using FakeHandle = GlHandle<FakeTraits>;

std::vector<std::string> g_restored;

class Owner : public IGpuRestorable {
public:
    Owner(std::string name, GpuResources::Phase phase) : IGpuRestorable(phase), _name(std::move(name)) {}
    void RestoreGpu() override {
        g_restored.push_back(_name);
        handle = FakeHandle::Create();
    }
    FakeHandle handle = FakeHandle::Create();

private:
    std::string _name;
};

} // namespace

TEST(GpuResourceRegistry, InvalidateEmptiesEveryLiveHandleWithoutDeletingIt) {
    FakeTraits::deleted.clear();
    const std::size_t before = GpuResources::LiveHandleCount();
    FakeHandle a = FakeHandle::Create();
    FakeHandle b = FakeHandle::Create();
    FakeHandle empty;
    EXPECT_EQ(GpuResources::LiveHandleCount(), before + 2) << "an empty handle is not tracked";

    FakeHandle moved = std::move(a);
    EXPECT_FALSE(a); // NOLINT(bugprone-use-after-move): moved-from is empty and untracked
    EXPECT_EQ(GpuResources::LiveHandleCount(), before + 2);
    b.reset();
    EXPECT_EQ(FakeTraits::deleted, std::vector<GLuint>{101});
    EXPECT_EQ(GpuResources::LiveHandleCount(), before + 1);

    EXPECT_EQ(GpuResources::InvalidateAll(), before + 1);
    EXPECT_FALSE(moved);
    EXPECT_EQ(GpuResources::LiveHandleCount(), 0u);
    EXPECT_EQ(FakeTraits::deleted, std::vector<GLuint>{101}) << "the dead context's names are never deleted";
}

TEST(GpuResourceRegistry, RecreateWalksOwnersByPhaseThenAge) {
    g_restored.clear();
    GpuResources::InvalidateAll();
    Owner vao("vao", GpuResources::Phase::VertexArrays);
    Owner fbo("fbo", GpuResources::Phase::Framebuffers);
    Owner texture1("texture1", GpuResources::Phase::Textures);
    Owner buffer("buffer", GpuResources::Phase::Buffers);
    Owner texture2("texture2", GpuResources::Phase::Textures);
    Owner moved("moved", GpuResources::Phase::Textures);
    Owner copy = std::move(moved); // the move target registers itself, after the others
    const std::size_t owners = GpuResources::RestorableCount();
    {
        const Owner temporary("temporary", GpuResources::Phase::Buffers);
        EXPECT_EQ(GpuResources::RestorableCount(), owners + 1);
    }
    EXPECT_EQ(GpuResources::RestorableCount(), owners) << "a destroyed owner unregisters";

    GpuResources::InvalidateAll();
    EXPECT_FALSE(vao.handle);
    const std::uint32_t generation = GpuResources::Generation();
    EXPECT_GE(GpuResources::RecreateAll(), 7u);
    EXPECT_EQ(GpuResources::Generation(), generation + 1);
    // The moved-from owner is alive, so still registered (with its name moved away).
    EXPECT_EQ(g_restored, (std::vector<std::string>{"buffer", "texture1", "texture2", "", "moved", "fbo", "vao"}));
    EXPECT_TRUE(vao.handle);
    EXPECT_TRUE(copy.handle);
}
