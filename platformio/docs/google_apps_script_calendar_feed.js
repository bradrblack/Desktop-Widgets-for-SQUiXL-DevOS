/**
 * SQUiXL Agenda card feed - Google Apps Script Web App
 * =====================================================
 *
 * Purpose
 * -------
 * The Agenda card (widget_calendar.cpp) fetches a plain .ics URL and shows
 * the next 6 upcoming events. Pointing it directly at your calendar's own
 * "secret address in iCal format" works, but that export contains your
 * entire calendar history - often well over a megabyte - which caused a
 * real production issue: the on-device HTTP fetch blocked inside a TLS
 * socket read long enough to trip the watchdog and reboot the device in a
 * loop (see CHANGELOG.md). It also isn't ordered chronologically, so
 * capping the read size doesn't reliably get you the *next* events anyway.
 *
 * This script solves both problems: it runs against the Calendar API on
 * Google's servers (under your own account, via "Execute as: Me"), filters
 * to a date range, sorts by start time, keeps only the soonest few, and
 * returns a tiny hand-built .ics feed containing just those events. Your
 * real calendar and its full history are never exposed - only whatever
 * this script chooses to output.
 *
 * Deploying it
 * ------------
 * 1. Go to https://script.google.com -> New project.
 * 2. Delete the placeholder code and paste this whole file in.
 * 3. If this isn't your default calendar, replace
 *    CalendarApp.getDefaultCalendar() below with
 *    CalendarApp.getCalendarById('your-calendar-id@group.calendar.google.com')
 *    (find the ID on that calendar's own settings page).
 * 4. Generate a shared key so the URL isn't the *only* thing protecting
 *    your feed: `openssl rand -hex 32` in a terminal, then paste the
 *    result in place of PASTE_YOUR_KEY_HERE below.
 * 5. Deploy -> New deployment -> gear icon -> Web app.
 *      - Execute as: Me
 *      - Who has access: Anyone
 *    ("Anyone" is required - the device can't sign in as a Google user, so
 *    the deployment URL plus the key above is what stands in for auth.
 *    Anyone with BOTH is still limited to seeing just the filtered feed
 *    below, never your real calendar.)
 * 6. Authorize the script when prompted (Google will warn it's
 *    "unverified" since it's your own script - Advanced -> Go to project).
 * 7. Copy the URL ending in /exec.
 * 8. In the SQUiXL web portal's Calendar Settings, set the iCal URL to
 *    that URL with the key appended, e.g.:
 *    https://script.google.com/macros/s/AKfycb.../exec?key=<your-key>
 *
 * After editing this script later, redeploy via Deploy -> Manage
 * deployments -> pencil icon -> Deploy - this keeps the same /exec URL
 * rather than minting a new one.
 */

var SHARED_KEY = 'PASTE_YOUR_KEY_HERE';

function doGet(e) {
  if (!e || !e.parameter || !e.parameter.key || e.parameter.key !== SHARED_KEY) {
    return ContentService.createTextOutput('').setMimeType(ContentService.MimeType.PLAIN_TEXT);
  }

  var calendar = CalendarApp.getDefaultCalendar();
  var now = new Date();
  var future = new Date();
  future.setDate(future.getDate() + 30); // look-ahead window

  var events = calendar.getEvents(now, future);
  events.sort(function (a, b) { return a.getStartTime() - b.getStartTime(); });

  var maxEvents = 6; // keep in sync with MAX_EVENTS in widget_calendar.cpp
  if (events.length > maxEvents) events = events.slice(0, maxEvents);

  var lines = ['BEGIN:VCALENDAR', 'VERSION:2.0', 'PRODID:-//SQUiXL Agenda Feed//EN'];

  events.forEach(function (ev) {
    lines.push('BEGIN:VEVENT');
    lines.push('SUMMARY:' + icsEscape(ev.getTitle()));
    if (ev.isAllDayEvent()) {
      lines.push('DTSTART;VALUE=DATE:' + formatDate(ev.getAllDayStartDate()));
    } else {
      lines.push('DTSTART:' + formatDateTime(ev.getStartTime()));
    }
    lines.push('END:VEVENT');
  });

  lines.push('END:VCALENDAR');

  return ContentService.createTextOutput(lines.join('\r\n'))
    .setMimeType(ContentService.MimeType.PLAIN_TEXT);
}

function icsEscape(text) {
  return text.replace(/\\/g, '\\\\').replace(/,/g, '\\,').replace(/;/g, '\\;');
}

function pad(n) { return (n < 10 ? '0' : '') + n; }

function formatDate(d) {
  return d.getFullYear() + pad(d.getMonth() + 1) + pad(d.getDate());
}

function formatDateTime(d) {
  // Local wall-clock time, no trailing Z - the on-device parser doesn't do
  // timezone conversion (see widget_calendar.h), so this matches it rather
  // than emitting UTC.
  return formatDate(d) + 'T' + pad(d.getHours()) + pad(d.getMinutes()) + pad(d.getSeconds());
}
