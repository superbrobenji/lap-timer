/* fixture: zero lap_set_venue()/lap_import_rtc() call sites -- must trip the zero-sites floor
 * (main() in venue_announce.py), regardless of --fail-on-violation. */
void unrelated(int flag)
{
    if (flag) {
        ui_post_venue(1, 0);
    }
}
