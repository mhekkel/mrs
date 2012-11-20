//---------------------------------------------------------------------
//
//	JavaScript for the Admin pages
//

Admin = {
	timeout: null,

	init: function() {
		$("table.select_form tr").each(function() {
			$(this).click(function()
			{
				var id = this.id;
			
				this.section.find("form").hide();
				this.section.find("#" + id).show();
				
				this.section.find("tr").removeClass('selected');
				$(this).addClass('selected');

				try {
					sessionStorage.setItem("selected-" + this.section.attr("id"), id);
				} catch (e) {}
			})
		});

		var v = sessionStorage.getItem('selected-adminpage');

		$("div.section").each(function() {
			var section = $(this);
			var id = section.attr("id");

			var s = sessionStorage.getItem('selected-' + id);

			$(this).find("tr").each(function() {
				this.section = section;

				if (this.id == null || this.id.length == 0)
					this.id = $(this).text();
				
				if (this.id == s)
					$(this).addClass('selected');
			});

			$(this).find("form").each(function() {
				this.section = section;
				if (this.id == s)
					$(this).show();
			});
			
			if (id != v)
				section.hide();
		});
		
		$("div.delete").click(function() {
			$(this).parent().parent().remove();
		});
	},

	changeStatsView: function(view) {
		
		$("div.section").each(function() {
			if (this.id == view)	{ $(this).show(); }
			else					{ $(this).hide(); }
		});

		$("div.nav li a").each(function() {
			if (this.id == view)	{ $(this).addClass("selected"); }
			else					{ $(this).removeClass("selected"); }
		});
		
		sessionStorage.setItem('selected-adminpage', view);
	},
	
	addLinkToFormat: function(form) {
		var lastRow = $('#' + form + " table tr:last");
		var newRow = lastRow.clone();
		
		newRow.find("div.delete").click(function() {
			$(this).parent().parent().remove();
		});
		
		lastRow.before(newRow);
		newRow.show();
	}
}

// register a load handler
addLoadEvent(Admin.init);
