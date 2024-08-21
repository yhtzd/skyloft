#ifndef _SKYLOFT_EXPERIMENT_RANDOM_H_
#define _SKYLOFT_EXPERIMENT_RANDOM_H_

void random_init(void);

int random_int_uniform_distribution(int start, int end);
double random_real_uniform_distribution(double start, double end);
int random_int_bionomial_distribution(int start, int end);
bool random_bernouli_distribution(double p);
void random_exponential_distribution_init(double lambda);
double random_exponential_distribution(void);

#endif