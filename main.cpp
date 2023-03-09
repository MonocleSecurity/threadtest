#include <algorithm>
#include <array>
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

const size_t num_threads = 32;
const size_t num_futures = num_threads + 1;
const size_t num_atomics = num_threads + 1;
const size_t num_pipes = num_threads + 1;
const size_t num_condition_variables = num_threads + 1;
const size_t num_sockets = num_threads + 1;

double standard_deviation(const std::vector<double>& values)
{
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  const double sq_sum = std::inner_product(values.begin(), values.end(), values.begin(), 0.0, [](double const& x, double const& y) { return x + y; }, [mean](double const& x, double const& y) { return (x - mean) * (y - mean); });
  return std::sqrt(sq_sum / values.size());
}

struct Results
{
  Results(const std::chrono::high_resolution_clock::time_point start, const std::chrono::high_resolution_clock::time_point end, const std::array<std::chrono::high_resolution_clock::time_point, num_threads>& times) :
    start_(start),
    end_(end),
    times_(times)
  {

  }

  const std::chrono::high_resolution_clock::time_point start_;
  const std::chrono::high_resolution_clock::time_point end_;
  const std::array<std::chrono::high_resolution_clock::time_point, num_threads> times_;
};

class Printer
{
 public:
   Printer(const std::string& name) :
     name_(name)
   {
   }
   
   void AddResults(const Results& results)
   {
     total_durations_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(results.end_ - results.start_).count());
     durations_.reserve(durations_.size() + results.times_.size());
     for (size_t i = 0; i < (results.times_.size() - 1); ++i)
     {
       const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(results.times_[i + 1u] - results.times_[i]).count();
       durations_.push_back(std::max(0.0, static_cast<double>(microseconds)));
     }
   }
  
   void Print()
   {
     const double stddev = standard_deviation(durations_);
     auto minmax = std::minmax_element(durations_.begin(), durations_.end());
     std::cout << name_ << std::endl;
     std::cout << "  min: " << static_cast<uint64_t>(*minmax.first) << std::endl;
     std::cout << "  max: " << static_cast<uint64_t>(*minmax.second) << std::endl;
     std::cout << "  mean: " << static_cast<uint64_t>((std::accumulate(total_durations_.begin(), total_durations_.end(), 0.0) / total_durations_.size()) / num_threads) << std::endl;
     std::cout << "  stddev: " << static_cast<uint64_t>(stddev) << std::endl;
   }

   const std::string name_;
   std::vector<double> total_durations_;
   std::vector<double> durations_;
};

Results future()
{
  // Initialise
  std::array<std::unique_ptr<std::promise<void>>, num_futures> promises;
  std::array<std::unique_ptr<std::future<void>>, num_futures> futures;
  for (size_t i = 0; i < num_futures; ++i)
  {
    promises[i] = std::make_unique<std::promise<void>>();
    futures[i] = std::make_unique<std::future<void>>(promises[i]->get_future());
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    std::promise<void>* promise = promises[i + 1u].get();
    std::future<void>* future = futures[i].get();
    threads[i] = std::thread([time, promise, future]()
      {
        future->get();
        const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
        if (promise)
        {
          promise->set_value();
        }
        *time = now;
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  promises.front()->set_value();
  futures.back()->get();
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  return Results(start, end, times);
}

Results mutex()
{
  // Initialise
  std::array<std::unique_ptr<std::mutex>, num_threads> mutexes;
  std::array<std::unique_ptr<int>, num_threads> values;
  for (size_t i = 0; i < num_threads; ++i)
  {
    mutexes[i] = std::make_unique<std::mutex>();
    values[i] = std::make_unique<int>(0);
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    std::mutex* currentmutex = mutexes[i].get();
    int* currentvalue = values[i].get();
    std::mutex* nextmutex = nullptr;
    int* nextvalue = nullptr;
    if ((i + 1u) < num_threads)
    {
      nextmutex = mutexes[i + 1u].get();
      nextvalue = values[i + 1u].get();
    }
    threads[i] = std::thread([time, currentmutex, currentvalue, nextmutex, nextvalue]()
      {
        while (true)
        {
          {
            std::lock_guard<std::mutex> lock(*currentmutex);
            if (*currentvalue)
            {
              const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
              if (nextmutex && nextvalue)
              {
                std::lock_guard<std::mutex> lock(*nextmutex);
                ++(*nextvalue);
              }
              *time = now;
              return;
            }
          }
          std::this_thread::yield();
        }
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  {
    std::lock_guard<std::mutex> lock(*mutexes[0]);
    ++(*values[0]);
  }
  threads.back().join();
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { if (thread.joinable()) { thread.join(); } });
  return Results(start, end, times);
}

Results atomic()
{
  // Initialise
  std::array<std::unique_ptr<std::atomic<bool>>, num_atomics> values;
  for (size_t i = 0; i < num_atomics; ++i)
  {
    values[i] = std::make_unique<std::atomic<bool>>(false);
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    std::atomic<bool>* currentvalue = values[i].get();
    std::atomic<bool>* nextvalue = values[i + 1u].get();
    threads[i] = std::thread([time, currentvalue, nextvalue]()
      {
        while (true)
        {
          if (*currentvalue)
          {
            const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
            *nextvalue = true;
            *time = now;
            return;
          }
          std::this_thread::yield();
        }
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  *values.front() = true;
  threads.back().join();
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { if (thread.joinable()) { thread.join(); } });
  return Results(start, end, times);
}

Results condition_variable()
{
  // Initialise
  std::array<std::unique_ptr<std::mutex>, num_condition_variables> mutexes;
  std::array<std::unique_ptr<std::condition_variable>, num_condition_variables> condition_variables;
  std::array<std::unique_ptr<bool>, num_condition_variables> values;
  for (size_t i = 0; i < num_pipes; ++i)
  {
    mutexes[i] = std::make_unique<std::mutex>();
    condition_variables[i] = std::make_unique<std::condition_variable>();
    values[i] = std::make_unique<bool>(false);
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    std::mutex* mutex = mutexes[i].get();
    std::condition_variable* condition_variable = condition_variables[i].get();
    bool* value = values[i].get();
    std::mutex* next_mutex = mutexes[i + 1u].get();
    std::condition_variable* next_condition_variable = condition_variables[i + 1u].get();
    bool* next_value = values[i + 1u].get();
    threads[i] = std::thread([time, mutex, condition_variable, value, next_mutex, next_condition_variable, next_value]()
      {
        {
          std::unique_lock<std::mutex> lock(*mutex);
          condition_variable->wait(lock, [value]() { return *value; });
        }
        const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
        {
          std::unique_lock<std::mutex> lock(*next_mutex);
          *next_value = true;
          next_condition_variable->notify_one();
        }
        *time = now;
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  {
    std::unique_lock<std::mutex> lock(*mutexes.front());
    *values.front() = true;
    condition_variables.front()->notify_one();
  }
  {
    std::unique_lock<std::mutex> lock(*mutexes.back());
    condition_variables.back()->wait(lock, [&values]() { return *values.back(); });
  }
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  return Results(start, end, times);
}

Results socket()
{
  // Initialise
  boost::asio::io_service io;
  std::array<std::unique_ptr<boost::asio::ip::tcp::socket>, num_sockets> write_sockets;
  std::array<std::unique_ptr<boost::asio::ip::tcp::socket>, num_sockets> read_sockets;
  std::array<std::unique_ptr<boost::asio::ip::tcp::acceptor>, num_sockets> acceptors;
  for (size_t i = 0; i < num_sockets; ++i)
  {
    const uint16_t port = static_cast<uint16_t>(8123u + i);
    write_sockets[i] = std::make_unique<boost::asio::ip::tcp::socket>(io);
    acceptors[i] = std::make_unique<boost::asio::ip::tcp::acceptor>(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
    boost::system::error_code err;
    write_sockets[i]->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), port), err);
    if (err)
    {
      abort();
    }
    std::unique_ptr<boost::asio::ip::tcp::socket> read_socket = std::make_unique<boost::asio::ip::tcp::socket>(acceptors[i]->accept(err));
    if (err)
    {
      abort();
    }
    read_sockets[i] = std::move(read_socket);
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    boost::asio::ip::tcp::socket* write_socket = write_sockets[i + 1u].get();
    boost::asio::ip::tcp::socket* read_socket = read_sockets[i].get();
    threads[i] = std::thread([time, write_socket, read_socket]()
      {
        char buf;
        if (read_socket->read_some(boost::asio::buffer(&buf, sizeof(buf))) != 1)
        {
          abort();
        }
        const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
        if (write_socket->write_some(boost::asio::buffer("a", 1)) != 1)
        {
          abort();
        }
        *time = now;
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  if (write_sockets.front()->write_some(boost::asio::buffer("a", 1)) != 1)
  {
    abort();
  }
  char buf;
  if (read_sockets.back()->read_some(boost::asio::buffer(&buf, sizeof(buf))) != 1)
  {
    abort();
  }
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  return Results(start, end, times);
}
Results pipe()
{
  // Initialise
  std::array<std::unique_ptr<boost::process::pipe>, num_pipes> pipes;
  for (size_t i = 0; i < num_pipes; ++i)
  {
    pipes[i] = std::make_unique<boost::process::pipe>();
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  std::array<std::chrono::high_resolution_clock::time_point, num_threads> times;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::chrono::high_resolution_clock::time_point* time = &times[i];
    boost::process::pipe* ipipe = pipes[i].get();
    boost::process::pipe* opipe = pipes[i + 1u].get();
    threads[i] = std::thread([time, ipipe, opipe]()
      {
        char buf;
        if (ipipe->read(&buf, sizeof(buf)) != 1)
        {
          abort();
        }
        const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
        if (opipe->write("a", 1) != 1)
        {
          abort();
        }
        *time = now;
      });
  }
  // Run
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  if (pipes.front()->write("a", 1) != 1)
  {
    abort();
  }
  char buf;
  if (pipes.back()->read(&buf, sizeof(buf)) != 1)
  {
    abort();
  }
  const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  return Results(start, end, times);
}

int main()
{
  Printer futures("Futures");
  Printer mutexes("Mutexes");
  Printer atomics("Atomics");
  Printer condition_variables("Condition Variable");
  Printer pipes("Pipes");
  Printer sockets("Sockets");
  for (int i = 0; i < 60; ++i)
  {
    futures.AddResults(future());
    mutexes.AddResults(mutex());
    atomics.AddResults(atomic());
    condition_variables.AddResults(condition_variable());
    pipes.AddResults(pipe());
    sockets.AddResults(socket());
  }
  futures.Print();
  mutexes.Print();
  atomics.Print();
  condition_variables.Print();
  pipes.Print();
  sockets.Print();
  return 0;
}

