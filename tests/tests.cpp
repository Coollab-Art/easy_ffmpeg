#include "glad/glad.h"
//
#include <glfw/include/GLFW/glfw3.h>
#include <imgui.h>
#include <easy_ffmpeg/easy_ffmpeg.hpp>
#include <exception>
#include <fstream>
#include <ios>
#include <quick_imgui/quick_imgui.hpp>
#include "exe_path/exe_path.h"

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

TEST_CASE("VideoDecoder")
{
    auto decoder = ffmpeg::VideoDecoder{exe_path::dir() / "test.gif"};
    decoder.move_to_next_frame(); // Get first frame
    decoder.move_to_next_frame(); // Get first frame
    decoder.move_to_next_frame(); // Get first frame
    decoder.move_to_next_frame(); // Get first frame
    auto const& frame = decoder.current_frame();
    CHECK(frame.width == 256);  // NOLINT(*avoid-do-while)
    CHECK(frame.height == 144); // NOLINT(*avoid-do-while)
    std::ofstream file{exe_path::dir() / "test.txt"};
    for (size_t i = 0; i < 4 * frame.width * frame.height; ++i)
    {
        auto const val = static_cast<uint8_t>(frame.data[0][i]);
        // if (val != 0 && val != 255)
        // {
        //     std::cout << std::to_string(val) << '\n';
        // }
        file << std::to_string(static_cast<uint8_t>(frame.data[0][i])) << '\n';
    }
    file.flush();
}

auto main(int argc, char* argv[]) -> int
{
    // av_log_set_level(AV_LOG_VERBOSE);
    {
        const int  exit_code              = doctest::Context{}.run(); // Run all unit tests
        const bool should_run_imgui_tests = argc < 2 || strcmp(argv[1], "-nogpu") != 0;
        if (
            should_run_imgui_tests
            && exit_code == 0 // Only open the window if the tests passed; this makes it easier to notice when some tests fail
        )
        {
            try
            {
                // auto decoder = ffmpeg::VideoDecoder{"C:/Users/fouch/Downloads/eric-head.gif"};
                // auto decoder = ffmpeg::VideoDecoder{"C:/Users/fouch/Downloads/Moteur-de-jeu-avec-sous-titres.mp4"};
                auto decoder = ffmpeg::VideoDecoder{exe_path::dir() / "test.gif"};
                // auto   decoder = ffmpeg::VideoDecoder{"C:/Users/fouch/Downloads/test.js"};
                // auto   decoder = ffmpeg::VideoDecoder{"C:/Users/fouch/Downloads/PONY PONY RUN RUN - HEY YOU [OFFICIAL VIDEO].mp3"};
                GLuint texture_id;

                quick_imgui::loop("easy_ffmpeg tests", [&]() {
                    decoder.move_to_next_frame();
                    auto const& frame = decoder.current_frame();
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
                    // decoder.set_time(glfwGetTime());
                    // decoder.move_to_next_frame();
                    // glDeleteTextures(1, &texture_id);
                    // auto const& frame = decoder.current_frame();
                    // texture_id        = make_texture(frame);

                    ImGui::Begin("easy_ffmpeg tests");
                    ImGui::Text("%.2f ms", 1000.f / ImGui::GetIO().Framerate);
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<void*>(static_cast<uint64_t>(texture_id))), ImVec2{900 * frame.width / (float)frame.height, 900});
                    ImGui::End();
                    ImGui::ShowDemoWindow();
                });
            }
            catch (std::exception const& e)
            {
                std::cout << e.what() << '\n';
                throw;
            }
        }
        return exit_code;
    }
}
