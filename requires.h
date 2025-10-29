#pragma once

#define REQUIRES(cond) if (!(cond)) { logger::error(#cond " " __FILE__ ":" + std::to_string(__LINE__)); return; }
#define REQUIRES_RET(cond, ret) if (!(cond)) { logger::error(#cond " " __FILE__ ":" + std::to_string(__LINE__)); return (ret); }
