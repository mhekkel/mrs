//---------------------------------------------------------------------
//
//	JavaScript to fill in a status page
//

Status =
{
	timeout: null,

	init: function()
	{
		Status.updateStatus();
		$("#status-menu").attr("href", "#");
		$("#status-menu").click(function(event)
		{
			Status.updateStatus();
		});

		// sort the list (this is on date only, newest first)
		var rows = jQuery.makeArray($("#ul-db-list > li"));

		var rowArray = [];
		for (var i = 0; i < rows.length; ++i)
			rowArray[i] = rows[i];

		rowArray.sort(function (a, b) {
			var ka = a.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "key").value;
			var kb = b.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "key").value;

			var d = 0;
			if (ka < kb) {
				d = 1;
			} else if (ka > kb) {
				d = -1;
			}
			
			return d;
		});
		
		$(rowArray).remove().appendTo($("#ul-db-list"));
	
		delete rowArray;
	},

	updateStatus: function()
	{
		jQuery.getJSON("ajax/status",
			function(data, status)
			{
				if (status == "success") 
					Status.updateList(data);
//				Status.timeout = setTimeout("Status.updateStatus()", 10000);
			}
		);		
	},

	updateList: function(stat)
	{
		for (i in stat)
		{
			var db = stat[i];
			
			var row = document.getElementById("db-" + db.id);
			if (row == null) continue;

			if (db.update == null)
			{
				$(row).find("#active").hide();
				$(row).find("#idle").show();
				row.className = '';
			}
			else
			{
				$(row).find("#active").show();
				$(row).find("#idle").hide();

				$(row).find("#update-status").text(db.update.stage != null ? db.update.stage : "");
				
				if (db.update.stage == 'scheduled')
					row.className = 'scheduled';
				else if (db.update.stage == 'listing files' || db.update.stage == 'rsync')
					row.className = 'active';
				else if (db.update.progress < 0)
					row.className = 'error';
				else
				{
					row.className = 'active';

					// HTML 5 canvas
					$(row).find("#update-progress").each(function()
					{
						var ctx = this.getContext('2d');
						if (ctx != null)
						{
							this.style.display = '';
		
							var p = db.update.progress * 100;
							if (p > 100)
								p = 100;
		
							ctx.strokeStyle = "#2f506c";
							ctx.strokeRect(0, 0, 102, 10);
		
							ctx.fillStyle = "#c6d4e1";
							ctx.fillRect(1, 1, p, 8);
							
							ctx.fillStyle = "#ffffff";
							ctx.fillRect(p + 1, 1, 100 - p, 8);
						}
					});
				}
			}
		}
	}
}

// register a load handler
addLoadEvent(Status.init);
