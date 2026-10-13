/* fixture: three bypasses the pre-M5 rule accepted, and the shape that must still pass. */
void sibling_branch(int flag, const trk_venue_t *v, uint16_t id)
{
    if (flag) {
        lap_set_venue(&s_lap, v);
    } else {
        ui_post_venue(id, 0);
    }
}

void announce_before(const trk_venue_t *v)
{
    ui_post_venue(v->id, 0);
    lap_set_venue(&s_lap, v);
}

void wrong_venue(const trk_venue_t *v)
{
    lap_set_venue(&s_lap, v);
    ui_post_venue(1234, 0);
}

void correct(const trk_venue_t *v)
{
    lap_set_venue(&s_lap, v);
    ui_post_venue(v->id, 0);
}
