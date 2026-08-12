cmake_minimum_required(VERSION 3.9)
project(Pip C)

# petpal.c is the app; pet.r adds the 'SIZE' resource (background flags).
# Produces Pip.dsk (drag onto Mini vMac), plus .bin / .APPL.
add_application(Pip petpal.c pet.r)
