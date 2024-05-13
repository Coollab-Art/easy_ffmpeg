#include <fstream>
#include <iostream>
#include "easy_ffmpeg/easy_ffmpeg.hpp"
#include "exe_path/exe_path.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

void check_equal(ffmpeg::Frame const& frame, std::filesystem::path const& path_to_expected_values)
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
        REQUIRE(frame.data[i] == expected_values[i]); // NOLINT(*avoid-do-while, *pointer-arithmetic)
}

TEST_CASE("VideoDecoder")
{
    auto decoder = ffmpeg::VideoDecoder{exe_path::dir() / "test.gif", AV_PIX_FMT_RGBA};
    check_equal(*decoder.get_frame_at(0., ffmpeg::SeekMode::Exact), exe_path::dir() / "expected_frame_0.txt");
    check_equal(*decoder.get_frame_at(0.13, ffmpeg::SeekMode::Exact), exe_path::dir() / "expected_frame_3.txt");
    std::cout << decoder.detailed_info();
}
