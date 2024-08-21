#include <random>

#include "random.h"

static std::mt19937 rng;

void random_init(void)
{
    srand(time(NULL));
    rng.seed(std::random_device()());
}

int random_int_uniform_distribution(int start, int end)
{
    std::uniform_int_distribution<int> dist(start, end);
    return dist(rng);
}

double random_real_uniform_distribution(double start, double end)
{
    std::uniform_real_distribution<double> dist(start, end);
    return dist(rng);
}

int random_int_bionomial_distribution(int start, int end)
{
    std::binomial_distribution<int> dist(start, end);
    return dist(rng);
}

bool random_bernouli_distribution(double p)
{
    std::bernoulli_distribution dist(p);
    return dist(rng);
}

static std::exponential_distribution exp_dist;

void random_exponential_distribution_init(double lambda)
{
    exp_dist = std::exponential_distribution(lambda);
}

double random_exponential_distribution(void)
{
    return exp_dist(rng);
}