cmake_minimum_required(VERSION 3.9)
project(OneFoe C)

# Produces OneFoe.dsk (drag onto Mini vMac), plus .bin / .APPL.
add_application(OneFoe onefoe.c)
