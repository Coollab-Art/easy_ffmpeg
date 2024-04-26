#include "glad/glad.h"
//
#include <glfw/include/GLFW/glfw3.h>
#include <imgui.h>
#include <easy_ffmpeg/easy_ffmpeg.hpp>
#include <quick_imgui/quick_imgui.hpp>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

auto make_texture(AVFrame const& frame) -> GLuint
{
    if (frame.width == 0)
        return 0;
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Upload pixel data to texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data[0]);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture

    return textureID;
}

auto main(int argc, char* argv[]) -> int
{
    // av_log_set_level(AV_LOG_VERBOSE);
    {
        const int  exit_code              = 0; // doctest::Context{}.run(); // Run all unit tests
        const bool should_run_imgui_tests = argc < 2 || strcmp(argv[1], "-nogpu") != 0;
        if (
            should_run_imgui_tests
            && exit_code == 0 // Only open the window if the tests passed; this makes it easier to notice when some tests fail
        )
        {
            // auto reader = ffmpeg::Capture{"C:/Users/fouch/Downloads/eric-head.gif"};
            auto   reader = ffmpeg::Capture{"C:/Users/fouch/Downloads/Moteur-de-jeu-avec-sous-titres.mp4"};
            GLuint texture_id;

            quick_imgui::loop("easy_ffmpeg tests", [&]() {
                reader.move_to_next_frame();
                auto const& frame = reader.current_frame();
                static bool first = true;
                if (first && frame.width != 0)
                {
                    texture_id = make_texture(frame);
                    first      = false;
                    glfwSwapInterval(0);
                }
                else
                {
                    if (frame.width != 0)
                    {
                        glBindTexture(GL_TEXTURE_2D, texture_id);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data[0]);
                    }
                }
                // reader.set_time(glfwGetTime());
                // reader.move_to_next_frame();
                // glDeleteTextures(1, &texture_id);
                // auto const& frame = reader.current_frame();
                // texture_id        = make_texture(frame);

                ImGui::Begin("easy_ffmpeg tests");
                ImGui::Text("%.2f ms", 1000.f / ImGui::GetIO().Framerate);
                ImGui::Image((ImTextureID)texture_id, ImVec2{900 * frame.width / (float)frame.height, 900});
                ImGui::End();
                ImGui::ShowDemoWindow();
            });
        }
        return exit_code;
    }
}
