#include <gtest/gtest.h>

#include <cassert>

#include <PCDMBackend.h>


using pCDM::t_FP;


class PCDMBackend_test : public ::testing::Test
{
public:
    std::array<std::vector<t_FP>, 2> genInputData(
        t_FP minValX, t_FP stepX, t_FP maxValX,
        t_FP minValY, t_FP stepY, t_FP maxValY)
    {
        std::array<std::vector<t_FP>, 2> mat;

        const t_FP e_x = stepX * 0.0001f;
        const t_FP e_y = stepY * 0.0001f;

        size_t countX, countY;
        t_FP x, y;
        for (x = minValX, countX = 0; x <= maxValX + e_x;
             ++countX, x = static_cast<t_FP>(countX) * stepX + minValX)
        {
            for (y = minValY, countY = 0; y <= maxValY + e_y;
                 ++countY, y = static_cast<t_FP>(countY) * stepY + minValY)
            {
                mat[0].push_back(x);
                mat[1].push_back(y);
            }
        }

        //for (t_FP x = minValX; x <= maxValX + e_x; x += stepX)
        //{
        //    for (t_FP y = minValY; y <= maxValY + e_y; y += stepY)
        //    {
        //        mat[0].push_back(x);
        //        mat[1].push_back(y);
        //    }
        //}

        return mat;
    }
};


TEST_F(PCDMBackend_test, test1)
{
    PCDMBackend backend;

    auto && input = genInputData(
        -7, 0.1f, 7,
        -5, 0.1f, 5);

    backend.setHorizontalCoords(input);
    ASSERT_EQ(PCDMBackend::State::parametersChanged, backend.state());

    PCDMBackend::Parameters params;
    params.sourceParameters.horizontalCoord = { 0.5f, -0.25f };
    params.sourceParameters.depth = 2.75f;
    params.sourceParameters.omega = { 5, -8, 30 };
    params.sourceParameters.dv = { 0.00144f, 0.00128f, 0.00072f };
    params.nu = 0.25f;

    backend.setParameters(params);
    ASSERT_EQ(PCDMBackend::State::parametersChanged, backend.state());

    backend.run();
    ASSERT_EQ(PCDMBackend::State::resultsReady, backend.state());

    const auto & results = backend.results();

    const auto ue0 = -4.8481476e-006f;
    const auto un0 = -2.9985717e-006f;
    const auto uv0 = 1.8188007e-006f;

    const auto ue1 = -4.9327205e-006f;
    const auto un1 = -2.9850895e-006f;
    const auto uv1 = 1.8489232e-006f;

    const auto ue14240 = 4.4342782e-006f;
    const auto un14240 = 3.5152977e-006f;
    const auto uv14240 = 1.9343227e-006f;

    ASSERT_EQ(results[0].size(), 14241);
    ASSERT_EQ(results[1].size(), 14241);
    ASSERT_EQ(results[2].size(), 14241);

    ASSERT_FLOAT_EQ(results[0][0], ue0);
    ASSERT_FLOAT_EQ(results[1][0], un0);
    ASSERT_FLOAT_EQ(results[2][0], uv0);

    ASSERT_FLOAT_EQ(results[0][1], ue1);
    ASSERT_FLOAT_EQ(results[1][1], un1);
    ASSERT_FLOAT_EQ(results[2][1], uv1);

    ASSERT_FLOAT_EQ(results[0][14240], ue14240);
    ASSERT_FLOAT_EQ(results[1][14240], un14240);
    ASSERT_FLOAT_EQ(results[2][14240], uv14240);
}
