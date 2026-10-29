cmake_minimum_required(VERSION 3.21)
project(Game LANGUAGES CXX)
find_package(GMEngine CONFIG REQUIRED)
gm_add_game_module(Game Scripts/game.cpp)
