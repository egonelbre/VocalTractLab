// ****************************************************************************
// System-theme-aware color helpers for the wxWidgets pictures.
//
// The picture classes used to call SetBackground(*wxWHITE_BRUSH), draw axes
// and labels with *wxBLACK_PEN, and let dc.DrawText() inherit the system
// default text foreground. On macOS dark mode that combination produces
// white text on a forced-white background — invisible.
//
// These helpers route every "default" pen / brush / text color through
// wxSystemSettings so light and dark themes both render correctly without
// per-call branches. Semantic colors (red lips, green track, blue spectrum
// curve) are left alone — they read on either background.
// ****************************************************************************

#ifndef GUI_UTIL_THEME_H_
#define GUI_UTIL_THEME_H_

#include <wx/brush.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/pen.h>
#include <wx/settings.h>

namespace Theme {

inline wxColour bgColour() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
}

inline wxColour fgColour() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

inline wxBrush bgBrush() { return wxBrush(bgColour()); }
inline wxBrush fgBrush() { return wxBrush(fgColour()); }
inline wxPen bgPen(int width = 1) { return wxPen(bgColour(), width); }
inline wxPen fgPen(int width = 1) { return wxPen(fgColour(), width); }

// Convenience: clear `dc` to the system window background and align the
// device context's text foreground with the system text color, so any
// later DrawText() call shows up on the freshly cleared canvas regardless
// of light/dark mode.
inline void clearAndPrepareDc(wxDC& dc) {
  dc.SetBackground(bgBrush());
  dc.Clear();
  dc.SetTextForeground(fgColour());
}

}  // namespace Theme

#endif  // GUI_UTIL_THEME_H_
