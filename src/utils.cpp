#include "utils.hpp"

namespace utils {
void backup_db(const sqlite::database &db, const std::string &backup_path) {
  // sqlite::database bkp(backup_path);
  // auto con = db.connection();
  // auto state = std::unique_ptr<sqlite3_backup, decltype(&sqlite3_backup_finish)>(sqlite3_backup_init(bkp.connection().get(), "main", con.get(), "main"), sqlite3_backup_finish);

  // if (state) {
  //   int rc;

  //   do {
  //     rc = sqlite3_backup_step(state.get(), 100);
  //   } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
  // }
}
}; // namespace utils
