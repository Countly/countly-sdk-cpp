#include "view.hpp"

std::string View::serialize() const {
	return object.dump();
}

View::View(const std::string& name) {
	object["key"] = "CLY_[veiw]";
	object["count"] = 1;
}