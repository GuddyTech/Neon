
#include "Neon/Neon.h"
#include "gtest/gtest.h"
#include "hostContainer.h"
#include "runHelper.h"

TEST(domain_hostContainter, dGrid)
{
    int nGpus = 3;
    using Type = int64_t;
    runAllTestConfiguration(std::function(hostContainer::run<Neon::dGrid, Type, 0>),
                            nGpus,
                            1);
}

TEST(domain_hostContainter, eGrid)
{
    int nGpus = 3;
    using Type = int64_t;
    runAllTestConfiguration(std::function(hostContainer::run<Neon::eGrid, Type, 0>),
                            nGpus,
                            1);
}

TEST(domain_hostContainter, bGrid)
{
    int nGpus = 3;
    using Type = int64_t;
    runAllTestConfiguration(std::function(hostContainer::run<Neon::bGrid, Type, 0>),
                            nGpus,
                            1);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    Neon::init();
    return RUN_ALL_TESTS();
}
