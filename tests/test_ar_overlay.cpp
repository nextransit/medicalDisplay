#include <gtest/gtest.h>
#include <cstring>
extern "C" {
#include "ar_overlay.h"
}

class AROverlayTest : public ::testing::Test {
protected:
    AROverlayEngine* ar = nullptr;
    void SetUp() override { ar = ar_engine_create(1920, 1080); }
    void TearDown() override { ar_engine_destroy(ar); }
};

TEST_F(AROverlayTest, CreateAndDestroy) {
    AROverlayEngine* a = ar_engine_create(640, 480);
    ASSERT_NE(a, nullptr);
    ar_engine_destroy(a);
}
TEST_F(AROverlayTest, AddAnnotationWorks) {
    ASSERT_NE(ar, nullptr);
    AnnotationData ann = {};
    ann.type = ANNOTATION_RECT;
    ann.x = 100.0f; ann.y = 200.0f;
    ann.width = 50.0f; ann.height = 50.0f;
    strcpy(ann.label, "test");
    int id = ar_engine_add_annotation(ar, &ann);
    EXPECT_GE(id, 1);
}
TEST_F(AROverlayTest, CreateSessionWorks) {
    ASSERT_NE(ar, nullptr);
    AnnotationSession session = {};
    EXPECT_EQ(ar_engine_create_session(ar, &session), 0);
}
TEST_F(AROverlayTest, RenderSucceeds) {
    ASSERT_NE(ar, nullptr);
    uint8_t frame[1920*1080*3];
    memset(frame, 0, sizeof(frame));
    uint8_t out[1920*1080*3];
    EXPECT_EQ(ar_engine_render(ar, frame, out), 0);
}
