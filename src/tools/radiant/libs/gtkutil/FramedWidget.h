#pragma once

#include <gtk/gtk.h>
#include <string>

#include "LeftAlignedLabel.h"

namespace gtkutil
{
	/** greebo: Encapsulation to create a framed, contained widget
	 *
	 * Pass the widget to the class constructor and use the operator GtkWidget* to retrieve
	 * the completed framed widget.
	 */
	class FramedWidget
	{
		protected:
			// The contained widget
			GtkWidget* _containedWidget;

			std::string _title;

		public:

			// Constructor
			FramedWidget (GtkWidget* containedWidget) :
				_containedWidget(containedWidget), _title("")
			{
			}

			// Constructor with frame label
			FramedWidget (GtkWidget* containedWidget, std::string& title) :
				_containedWidget(containedWidget), _title(title)
			{
			}

			// Constructor with frame label
			FramedWidget (GtkWidget* containedWidget, const char* title) :
				_containedWidget(containedWidget), _title(title)
			{
			}

			virtual ~FramedWidget ()
			{
			}

			// Operator cast to GtkWindow* (use this to create and retrieve the GtkWidget* pointer)
			virtual operator GtkWidget* ()
			{
				// Create a new frame and set its properties
				GtkFrame* frame;

				if (!_title.empty())
					frame = GTK_FRAME(gtk_frame_new(_title.c_str()));
				else
					frame = GTK_FRAME(gtk_frame_new(0));
				gtk_widget_show(GTK_WIDGET(frame));
				gtk_frame_set_shadow_type(frame, GTK_SHADOW_IN);

				// Add the contained widget as children to the frame
				gtk_container_add(GTK_CONTAINER(frame), _containedWidget);
				gtk_widget_show(_containedWidget);

				// Now show the whole widget tree
				gtk_widget_show_all(GTK_WIDGET(frame));

				// Return the readily fabricated widget
				return GTK_WIDGET(frame);
			}
	};

} // namespace gtkutil
