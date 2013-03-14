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
		
		// Poll the blast queue
		Admin.poll();
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
	},
	
	// Blast Queue management
	
	poll: function()
	{
		if (Admin.timeout != null)
			clearTimeout(Admin.timeout);
		Admin.timeout = null;
	
		try
		{
			jQuery.getJSON("ajax/blast/queue",
				function(data, status) {
					if (status == "success") 
					{
						if (data.error != null) {
							alert("Retrieving blast queue failed:\n" + data.error);
						} else {
							Admin.updateBlastQueue(data);
						}
					}
				});
		} catch (e) {}
	
		Admin.timeout = setTimeout("Admin.poll();", 10000);
	},
	
	updateBlastQueue: function(data)
	{
		var table = document.getElementById("blastQueue");
	
		// remove previous results
		while (table.tBodies[0].rows.length > 0) {
			table.tBodies[0].deleteRow(table.tBodies[0].rows.length - 1);
		}
		
		for (ix in data) {
			var job = data[ix];
		
			var row = table.tBodies[0].insertRow(-1);
			
			row.className = 'clickable';
			row.id = job.id;

			// ID
			cell = row.insertCell(row.cells.length);
			$(cell).text(job.id);
			cell.className = 'jobID';
			
			// query length
			cell = row.insertCell(row.cells.length);
			$(cell).text(job.queryLength).attr('style', 'text-align:right');
			cell.className = 'nr';
			
			// databank
			cell = row.insertCell(row.cells.length);
			$(cell).text(job.db);
			
			// status
			cell = row.insertCell(row.cells.length);
			$(cell).text(job.status);
			
			if (job.status == 'running')
				$(row).addClass('active');
			else if (job.status == 'queued')
				$(row).addClass('scheduled');
			
/*			// HTML 5 canvas
			var canvas = document.createElement('canvas');
			if (canvas != null && canvas.getContext != null) {
				cell.appendChild(canvas);
				
				canvas.height = 4;
				canvas.width = 100;
				
				var ctx = canvas.getContext('2d');
				if (ctx != null)
				{
					if (hit.coverage.start > 0)
					{
						ctx.fillStyle = '#CCCCCC';
						ctx.fillRect(0, 0, hit.coverage.start, 4);
					}
					
					ctx.fillStyle = BlastResult.colorTable[hit.coverage.color - 1];
					ctx.fillRect(hit.coverage.start, 0, hit.coverage.start + hit.coverage.length, 4);
					
					if (hit.coverage.start + hit.coverage.length < 100)
					{
						ctx.fillStyle = '#CCCCCC';
						ctx.fillRect(hit.coverage.start + hit.coverage.length, 0, 100, 4);
					}
				}
			}
	*/
			// remove link
			cell = row.insertCell(4);
			
			// work around msie missing features
			if (jQuery.browser.msie == null || jQuery.browser.version > 8) {
				cell.className = 'c5 delete';
			} else {
				cell.className = 'c5';
			}
			
			// apparently, a td is not clickable?
			var img = document.createElement('img');
			img.src = 'images/edit-delete.png';
			
			img.jobId = job.id;
			jQuery(img).click(function() {
				Admin.deleteJob(this.jobId);
				return false;
			});
			cell.appendChild(img);
		}
	},
	
	deleteJob: function(id)
	{
		try
		{
			jQuery.getJSON("ajax/blast/delete", { job: id },
				function(data, status) {
					if (status == "success") 
					{
						Admin.poll();
					
						if (data.error != null) {
							alert("Deleting blast job failed:\n" + data.error);
						}
					}
				});
		} catch (e) {}
	}
	
}

// register a load handler
addLoadEvent(Admin.init);
