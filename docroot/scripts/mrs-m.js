/* JavaScript for mobile version */

var M6M = {
	callActive: false,
	
	init: function(hitCount, db, q) {
		var results = $("#tabel");

		if (results != null && hitCount > 0)
		{
			$(window).scroll(function()
			{
				if (M6M.callActive)
					return;
				M6M.callActive = true;

				var pageHeight = document.documentElement.scrollHeight;
				var clientHeight = document.documentElement.clientHeight;
				if ((pageHeight - ($(window).scrollTop() + clientHeight)) < 50)
					M6M.extend(results, hitCount, db, q);
				else
					M6M.callActive = false;
			});
		}
	},
	
	extend: function(results, hitCount, db, q) {
		var rowCount = results.find("tr").length - 1;
		var count = hitCount - rowCount;
		if (count > 25)
			count = 25;
		if (rowCount < hitCount)
		{
			callActive = true;
		
			jQuery.post("ajax/search", {
				db: db, q: q, offset: rowCount, count: count
			}, function(data, status, jqXHR) {

				if (status == "success")
				{
					if (data.error != null && data.error.length > 0) {
						alert(data.error);
					}

					$(data.hits).each(function()
					{
						var lastRow = results.find("tr:last");
						var newRow = lastRow.clone();
						var cells = newRow.find("td");
						
						cells.eq(0).text(this.nr);
						
						var a = cells.eq(1).find("a");
						a.text(this.id);
						a.attr("href", "entry?db=" + db + "&id=" + this.id);

						cells.eq(2).text(this.title);
						
						lastRow.after(newRow);
						newRow.show();
					});
				}
				M6M.callActive = false;
			}, "json");
		}
		else
			M6M.callActive = false;
	}
};
