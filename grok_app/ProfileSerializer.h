// ProfileSerializer.h
#pragma once
#include "ProfileItem.h"
#include <boost/json.hpp>

boost::json::object ToJson(const ProfileItem& item);
ProfileItem FromJson(const boost::json::value& jv);