#include "models/files_model.hpp"

#include <utility>

namespace files {

FilesModel::FilesModel(std::string start_directory) : _browser(std::move(start_directory))
{
}

}  // namespace files
