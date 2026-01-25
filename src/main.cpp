#include <iostream>
#include <string>
#include <filesystem>
#include "Repository.hpp"
#include "Cloner.hpp"

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string command = argv[1];

    try {
        if (command == "init") {
            Repository::init();
            std::cout << "Initialized git directory\n";
        } 
        else if (command == "cat-file") {
            if (argc < 4 || std::string(argv[2]) != "-p") return EXIT_FAILURE;
            std::cout << Repository::read_object_from_disk(argv[3]);
        }
        else if (command == "hash-object") {
            if (argc < 4 || std::string(argv[2]) != "-w") return EXIT_FAILURE;
            std::filesystem::path inputPath(argv[3]);
            if (!std::filesystem::exists(inputPath)) return EXIT_FAILURE;
            std::cout << Repository::create_blob(inputPath) << "\n";
        }
        else if (command == "ls-tree") {
            if (argc < 4 || std::string(argv[2]) != "--name-only") return EXIT_FAILURE;
            std::string content = Repository::read_object_from_disk(argv[3]);
            size_t cursor = 0;
            while (cursor < content.size()) {
                size_t space_pos = content.find(' ', cursor);
                if (space_pos == std::string::npos) break;
                size_t null_pos = content.find('\0', space_pos + 1);
                if (null_pos == std::string::npos) break;
                std::string name = content.substr(space_pos + 1, null_pos - (space_pos + 1));
                std::cout << name << "\n";
                cursor = null_pos + 1 + 20; 
            }
        }
        else if (command == "write-tree") {
            std::cout << Repository::write_tree_recursive(std::filesystem::current_path()) << "\n";
        }
        else if (command == "commit-tree") {
            if (argc < 4) return EXIT_FAILURE;
            std::string tree_sha = argv[2];
            std::string parent_sha, message;
            for (int i = 3; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "-p" && i + 1 < argc) parent_sha = argv[++i];
                else if (arg == "-m" && i + 1 < argc) message = argv[++i];
            }
            std::cout << Repository::commit_tree(tree_sha, parent_sha, message) << "\n";
        }
        else if (command == "clone") {
            if (argc < 4) return EXIT_FAILURE;
            Cloner::clone_repo(argv[2], argv[3]);
        }
        else {
            std::cerr << "Unknown command " << command << '\n';
            return EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}