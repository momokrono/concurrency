# Performance Benchmarking for Task System

## Baseline Performance
Current performance as of initial implementation (with benchmarks):

- **Small tasks throughput**: ~3.4 million tasks/sec (100,000 tasks)
- **Large tasks throughput**: ~1.0 million tasks/sec (10,000 compute-intensive tasks)
- **Single task latency**: ~0 microseconds average (1,000 samples)
- **Task creation overhead**: ~1.5 million tasks/sec with sync (100,000 tasks)

## How to Run Benchmarks

### Setup
```bash
mkdir build && cd build
cmake .. 
make benchmarks
```

### Execute Benchmarks
```bash
./benchmarks
```

### To Run Before/After Comparison
1. Run benchmarks on current version: `./benchmarks > before.txt`
2. Make code changes
3. Rebuild: `make benchmarks`
4. Run benchmarks on modified version: `./benchmarks > after.txt`
5. Compare results: `diff before.txt after.txt`

## Benchmark Categories

### Throughput Tests
- **Small tasks**: Tests minimal atomic counter increment tasks
- **Large tasks**: Tests more compute-intensive tasks (1000 iterations)

### Latency Tests
- **Single task latency**: Measures time from task submission to execution start

### Overhead Tests
- **Task creation overhead**: Measures performance with synchronization

### Work-Stealing Tests
- **Efficiency test**: Creates uneven workload to test work-stealing algorithm

## Performance Targets
- Minimal performance degradation when adding new features (exception safety, waiting mechanisms, etc.)
- Maintain high throughput (> 1M tasks/sec for simple operations)
- Keep low latency for task execution