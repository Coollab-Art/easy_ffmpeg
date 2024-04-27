#include "glad/glad.h"
//
#include <glfw/include/GLFW/glfw3.h>
#include <imgui.h>
#include <libavutil/frame.h>
#include <cstdint>
#include <easy_ffmpeg/easy_ffmpeg.hpp>
#include <exception>
#include <fstream>
#include <quick_imgui/quick_imgui.hpp>
#include <stdexcept>
#include "exe_path/exe_path.h"
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

void check_equal(AVFrame const& frame, std::filesystem::path const& path_to_expected_values)
{
    static constexpr size_t expected_width  = 256;
    static constexpr size_t expected_height = 144;
    CHECK(frame.width == expected_width);   // NOLINT(*avoid-do-while)
    CHECK(frame.height == expected_height); // NOLINT(*avoid-do-while)

    std::vector<uint8_t> expected_values;
    {
        auto file = std::ifstream{path_to_expected_values};
        auto line = std::string{};
        while (std::getline(file, line))
            expected_values.push_back(static_cast<uint8_t>(std::stoi(line)));
        REQUIRE(expected_values.size() == 4 * expected_width * expected_height); // NOLINT(*avoid-do-while)
    }

    for (size_t i = 0; i < 4 * static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height); ++i)
        REQUIRE(frame.data[0][i] == expected_values[i]); // NOLINT(*avoid-do-while, *pointer-arithmetic)
}

// TEST_CASE("VideoDecoder")
// {
//     auto decoder = ffmpeg::VideoDecoder{exe_path::dir() / "test.gif"};
//     std::ignore  = decoder.move_to_next_frame(); // Get first frame
//     check_equal(decoder.current_frame(), exe_path::dir() / "expected_frame_0.txt");
//     std::ignore = decoder.move_to_next_frame();
//     std::ignore = decoder.move_to_next_frame();
//     std::ignore = decoder.move_to_next_frame();
//     check_equal(decoder.current_frame(), exe_path::dir() / "expected_frame_3.txt");
// }

auto make_texture(AVFrame const& frame) -> GLuint
{
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
                    auto const& frame = decoder.get_frame_at(glfwGetTime());
                    // decoder.seek_to(static_cast<int64_t>((/* 50.f - */ glfwGetTime()) * 1'000'000'000.f));
                    // if (!decoder.move_to_next_frame())
                    // {
                    //     decoder.seek_to_start();
                    //     if (!decoder.move_to_next_frame())
                    //         throw std::runtime_error{"This video has 0 frames!"};
                    // }
                    // auto const& frame = decoder.current_frame();
                    static bool first = true;
                    if (first)
                    {
                        texture_id = make_texture(frame);
                        first      = false;
                        glfwSwapInterval(0);
                    }
                    else
                        {
                            glBindTexture(GL_TEXTURE_2D, texture_id);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data[0]);
                        }
                    // decoder.set_time(glfwGetTime());
                    // decoder.move_to_next_frame();
                    // glDeleteTextures(1, &texture_id);
                    // auto const& frame = decoder.current_frame();
                    // texture_id        = make_texture(frame);

                    ImGui::Begin("easy_ffmpeg tests");
                    ImGui::Text("%.2f ms", 1000.f / ImGui::GetIO().Framerate);
                    ImGui::Text("Time: %.2f", glfwGetTime());
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<void*>(static_cast<uint64_t>(texture_id))), ImVec2{900.f * static_cast<float>(frame.width) / static_cast<float>(frame.height), 900.f});
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
