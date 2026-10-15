/* fixture: bypasses the venue-announce rule must reject (three from the PR #101 review M5, plus
 * the nested-conditional and shared-field-name ones from the F4 final review M5), and the shapes
 * that must still pass (correct, comma_in_nested_call, success_branch). */
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

void nested_conditional(int flag, const trk_venue_t *v)
{
    lap_set_venue(&s_lap, v);
    if (flag) {
        ui_post_venue(v->id, 0);
    }
}

void field_name_only(const trk_venue_t *v, const other_t *o)
{
    lap_set_venue(&s_lap, v);
    ui_post_venue(o->venue, 0);
}

void comma_in_nested_call(const trk_venue_t *v)
{
    lap_set_venue(&s_lap, pick(v->id, 1));
    ui_post_venue(pick(v->id, 1), 0);
}

void success_branch(const lap_rtc_t *lr)
{
    if (lap_import_rtc(&s_lap, lr) == 0) {
        ui_post_venue(lr->venue_id, 0);
    }
}
