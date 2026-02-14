#include <exception>
#include <iostream>
#include <string>

#include "app/DemoApp.hpp"

int main(int argc, char** argv) {
  std::string fbxPath;
  if (argc >= 2) {
    fbxPath = argv[1];
  }

  try {
    vv::DemoApp app;
    return app.Run(fbxPath);
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 2;
  }
}
