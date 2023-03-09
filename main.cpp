#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

const size_t num_threads = 16;//TODO
const size_t num_futures = num_threads + 1;
const size_t num_pipes = num_threads + 1;
const size_t num_condition_variables = num_threads + 1;
const size_t num_sockets = num_threads + 1;

void Atomic(const std::chrono::milliseconds loop_delay)
{
  // Initialise
  std::array<std::unique_ptr<std::atomic<bool>>, num_threads> values;
  for (size_t i = 0; i < num_threads; ++i)
  {
    values[i] = std::make_unique<std::atomic<bool>>(false);
  }
  // Setup
  std::array<std::thread, num_threads> threads;
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::atomic<bool>* currentvalue = values[i].get();
    std::atomic<bool>* nextvalue = nullptr;
    if ((i + 1u) < num_threads)
    {
      nextvalue = values[i + 1u].get();
    }
    threads[i] = std::thread([loop_delay, currentvalue, nextvalue]()
      {
        while (true)
        {
          {
            if (*currentvalue)
            {
              if (nextvalue)
              {
                *nextvalue = true;
              }
              return;
            }
          }
          if (loop_delay == std::chrono::milliseconds::zero())
          {
            std::this_thread::yield();
          }
          else
          {
            std::this_thread::sleep_for(loop_delay);
          }
        }
      });
  }
  // Run
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  *values[0] = true;
  threads.back().join();
  const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::cout << "Atomic latency" << (loop_delay == std::chrono::milliseconds::zero() ? "(yield)" : "(" + std::to_string(loop_delay.count()) + "ms)") << ": " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { if (thread.joinable()) { thread.join(); } });
}

void MutexVector(const std::chrono::milliseconds loop_delay)
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
  for (size_t i = 0; i < num_threads; ++i)
  {
    std::mutex* currentmutex = mutexes[i].get();
    int* currentvalue = values[i].get();
    std::mutex* nextmutex = nullptr;
    int* nextvalue = nullptr;
    if ((i + 1u) < num_threads)
    {
      nextmutex = mutexes[i + 1u].get();
      nextvalue = values[i + 1u].get();
    }
    threads[i] = std::thread([loop_delay, currentmutex, currentvalue, nextmutex, nextvalue]()
      {
        while (true)
        {
          {
            std::lock_guard<std::mutex> lock(*currentmutex);
            if (*currentvalue)
            {
              if (nextmutex && nextvalue)
              {
                std::lock_guard<std::mutex> lock(*nextmutex);
                ++(*nextvalue);
              }
              return;
            }
          }
          if (loop_delay == std::chrono::milliseconds::zero())
          {
            std::this_thread::yield();
          }
          else
          {
            std::this_thread::sleep_for(loop_delay);
          }
        }
      });
  }
  // Run
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(*mutexes[0]);
    ++(*values[0]);
  }
  threads.back().join();
  const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::cout << "Mutex latency" << (loop_delay == std::chrono::milliseconds::zero() ? "(yield)" : "(" + std::to_string(loop_delay.count()) + "ms)") << ": " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
  std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { if (thread.joinable()) { thread.join(); } });
}

int main()
{
  // Future/promise combo
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
    for (size_t i = 0; i < num_threads; ++i)
    {
      std::promise<void>* promise = promises[i + 1u].get();
      std::future<void>* future = futures[i].get();
      threads[i] = std::thread([promise, future]()
        {
          future->get();
          if (promise)
          {
            promise->set_value();
          }
        });
    }
    // Run
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    promises.front()->set_value();
    futures.back()->get();
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // Log and clean up
    std::cout << "Future latency: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  }
  // Mutex+Vector
  MutexVector(std::chrono::milliseconds(0)); // yield()
  MutexVector(std::chrono::milliseconds(10)); // sleep_for()
  MutexVector(std::chrono::milliseconds(40)); // sleep_for()
  // Atomic
  Atomic(std::chrono::milliseconds(0)); // yield()
  Atomic(std::chrono::milliseconds(10)); // sleep_for()
  Atomic(std::chrono::milliseconds(40)); // sleep_for()
  // Conditional Variable
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
    for (size_t i = 0; i < num_threads; ++i)
    {
      std::mutex* mutex = mutexes[i].get();
      std::condition_variable* condition_variable = condition_variables[i].get();
      bool* value = values[i].get();
      std::mutex* next_mutex = mutexes[i + 1u].get();
      std::condition_variable* next_condition_variable = condition_variables[i + 1u].get();
      bool* next_value = values[i + 1u].get();
      threads[i] = std::thread([mutex, condition_variable, value, next_mutex, next_condition_variable, next_value]()
        {
          {
            std::unique_lock<std::mutex> lock(*mutex);
            condition_variable->wait(lock, [value]() { return *value; });
          }
          std::unique_lock<std::mutex> lock(*next_mutex);
          *next_value = true;
          next_condition_variable->notify_one();
        });
    }
    // Run
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    {
      std::unique_lock<std::mutex> lock(*mutexes.front());
      *values.front() = true;
      condition_variables.front()->notify_one();
    }
    {
      std::unique_lock<std::mutex> lock(*mutexes.back());
      condition_variables.back()->wait(lock, [&values]() { return *values.back(); });
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // Log and clean up
    std::cout << "Conditional Variable latency: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  }
  // Pipes
  {
    // Initialise
    std::array<std::unique_ptr<boost::process::pipe>, num_pipes> pipes;
    for (size_t i = 0; i < num_pipes; ++i)
    {
      pipes[i] = std::make_unique<boost::process::pipe>();
    }
    // Setup
    std::array<std::thread, num_threads> threads;
    for (size_t i = 0; i < num_threads; ++i)
    {
      boost::process::pipe* ipipe = pipes[i].get();
      boost::process::pipe* opipe = pipes[i + 1u].get();
      threads[i] = std::thread([ipipe, opipe]()
        {
          if (ipipe)
          {
            char buf;
            if (ipipe->read(&buf, sizeof(buf)) != 1)
            {
              abort();
            }
          }
          if (opipe->write("a", 1) != 1)
          {
            abort();
          }
        });
    }
    // Run
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (pipes.front()->write("a", 1) != 1)
    {
      abort();
    }
    char buf;
    if (pipes.back()->read(&buf, sizeof(buf)) != 1)
    {
      abort();
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // Log and clean up
    std::cout << "Pipe latency: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  }
  // Sockets
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
    for (size_t i = 0; i < num_threads; ++i)
    {
      boost::asio::ip::tcp::socket* write_socket = write_sockets[i + 1u].get();
      boost::asio::ip::tcp::socket* read_socket = read_sockets[i].get();
      threads[i] = std::thread([write_socket, read_socket]()
        {
          if (read_socket)
          {
            char buf;
            if (read_socket->read_some(boost::asio::buffer(&buf, sizeof(buf))) != 1)
            {
              abort();
            }
          }
          if (write_socket->write_some(boost::asio::buffer("a", 1)) != 1)
          {
            abort();
          }
        });
    }
    // Run
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (write_sockets.front()->write_some(boost::asio::buffer("a", 1)) != 1)
    {
      abort();
    }
    char buf;
    if (read_sockets.back()->read_some(boost::asio::buffer(&buf, sizeof(buf))) != 1)
    {
      abort();
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // Log and clean up
    std::cout << "Socket latency: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
  }
  return 0;
}

