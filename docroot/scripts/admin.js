//---------------------------------------------------------------------
//
//	JavaScript for the Admin pages
//

Admin = {
	timeout: null,

	init: function()
	{
		$("div.nav a").each(function()
		{
			$(this).click(function()
			{
				var section = this.id;
				Admin.selectSection(section);
			});
		});

		$("div.section").each(function()
		{
			var section = $(this);
			var sectionId = section.attr("id");

			$(this).find("table.select_form tr").each(function()
			{
				if (this.id == null || this.id.length == 0)
					this.id = $(this).text();
				var rowId = this.id;
				$(this).click(function() {
					Admin.selectForm(sectionId, rowId);
				});
			});
		});
		
		$("div.delete").click(function()
		{
			$(this).parent().parent().remove();
		});

		$("select[name='format']").each(function()
		{
			if ($(this).val() == 'xml')
				$(this).next().show();
		});
		
		var v = sessionStorage.getItem('selected-adminpage');
		if (v == null)
			v = 'global';
		Admin.selectSection(v);
	},
	
	selectSection: function(section)
	{
		$("div.section").each(function()
		{
			if (this.id == section)	{ $(this).show(); }
			else					{ $(this).hide(); }
		});

		$("div.nav li a").each(function()
		{
			if (this.id == section)	{ $(this).addClass("selected"); }
			else					{ $(this).removeClass("selected"); }
		});
		
		sessionStorage.setItem('selected-adminpage', section);
		
		var f = Admin.selectedForm(section);
		if (f != '')
			Admin.selectForm(section, f);
	},
	
	selectForm: function(section, id)
	{
		var sect = $("#" + section + ".section");
	
		sect.find("form.admin_form").hide();
		sect.find("#" + id).show();
		
		sect.find("table.select_form tr").each(function()
		{
			if (this.id == id)		{ $(this).addClass('selected'); }
			else					{ $(this).removeClass('selected'); }
		});
		
		sect.find("form.add-del input[name='selected']").attr("value", id);
		
		try {
			sessionStorage.setItem("selected-" + section, id);
		} catch (e) {}
	},

	addLinkToFormat: function(form)
	{
		var lastRow = $('#' + form + " table tr:last");
		var newRow = lastRow.clone();
		
		newRow.find("div.delete").click(function()
		{
			$(this).parent().parent().remove();
		});
		
		lastRow.before(newRow);
		newRow.show();
		$('#' + form + " table").show();
	},
	
	addAliasToDb: function(db)
	{
		var table = $('#' + db + "-aliases");
		var lastRow = table.find("tr:last");
		var newRow = lastRow.clone();
		
		newRow.find("div.delete").click(function()
		{
			$(this).parent().parent().remove();
		});
		
		lastRow.before(newRow);
		newRow.show();
		table.show();
	},
	
	changeFormat: function(db)
	{
		var selected = $('#' + db + " select[name='format']").val();
		var stylesheetrow = $('#' + db + " input[name='stylesheet']");
		
		if (selected == 'xml')
			stylesheetrow.show('fast');
		else
			stylesheetrow.hide('fast');
	},
	
	selectedForm: function(section)
	{
		var result = sessionStorage.getItem("selected-" + section);
		if (result == null)
		{
			var sect = $('#' + section + '.section');
			result = sect.find("table.select_form tr:first td:first").text();
		}
		return result;
	}
}

// register a load handler
addLoadEvent(Admin.init);
