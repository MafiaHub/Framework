project(sfd C)
add_library(sfd STATIC sfd.c)
target_include_directories(sfd PRIVATE ".")
