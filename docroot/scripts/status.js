//---------------------------------------------------------------------
//
//	JavaScript to fill in a status page
//

Status = {
	timeout: null,

	init: function() {
		Status.updateStatus();
	},

	updateStatus: function() {
		jQuery.getJSON("ajax/status", { method: Status.viewing },
			function(data, status) {
				if (status == "success") 
				{
					Status.updateList(data);
					Status.timeout = setTimeout("Status.updateStatus()", 10000);
				}
			}
		);
	},
	
	updateList: function(stat) {
		if (stat.length + 1 != document.getElementById('databanks').rows.length) {
			window.location.reload();
			return;
		}
	
		for (i in stat) {
			var db = stat[i];
			
			var row = document.getElementById("db-" + db.name);
			if (row == null) continue;

			if (db.update != null)
			{
				if (db.update.progress < 0)
				{
					row.className = 'error';
					row.cells[6].children[0].innerHTML = db.update.stage;
					row.cells[6].children[1].style.display = 'none';
				}
				else
				{
					row.className = 'active';
					row.cells[6].children[0].innerHTML = db.update.stage;
					
					// HTML 5 canvas
					var bar = row.cells[6].children[1];
					var ctx = bar.getContext('2d');
					if (ctx != null) {
						bar.style.display = '';
	
						var p = db.update.progress * 98;
						if (p > 98)
							p = 98;
	
						ctx.strokeStyle = "#2f506c";
						ctx.strokeRect(0, 0, 100, 10);
	
						ctx.fillStyle = "#c6d4e1";
						ctx.fillRect(1, 1, p, 8);
						
						ctx.fillStyle = "#ffffff";
						ctx.fillRect(p + 1, 1, 98 - p - 1, 8);
					}
				}
			} else {
				row.className = '';
				row.cells[6].children[0].innerHTML = '';
				row.cells[6].children[1].style.display = 'none';
			}
		}
	},

/*	
	updateUpdate: function(stat) {
		if (stat.length + 1 != document.getElementById('update').rows.length) {
			window.location.reload();
			return;
		}

		for (i in stat) {
			var db = stat[i];
			var row = document.getElementById("update-" + db.name);
			if (row == null) continue;

			row.cells[1].innerHTML = db.fetchDate;
			row.cells[3].innerHTML = db.buildDate;
			
			if (db.failed) {
				row.className = 'error';
			} else if (db.building || db.fetching) {
				row.className = 'active';
			} else {
				row.className = '';
			}
			
			if (db.fetching) {
				row.cells[1].className = 'active';
				row.cells[2].className = 'active';
			} else {
				row.cells[1].className = '';
				row.cells[2].className = '';
			}

			if (db.building) {
				row.cells[3].className = 'active';
				row.cells[4].className = 'active';
			} else {
				row.cells[3].className = '';
				row.cells[4].className = '';
			}
		}
	},
*/	
	sortTable: function(table, column) {
		var t = document.getElementById(table);
		var rows = t.rows;
		if (rows == null)
			return;
		
		var desc = ! t.sortDescending;
		if (t.sortedOnColumn != column) {
			desc = false;
			t.sortedOnColumn = column;
		}
		t.sortDescending = desc;
		
		var rowArray = [];
		for (var i = 1; i < rows.length; ++i)
			rowArray[i - 1] = rows[i];
		
		rowArray.sort(function (a, b) {
			var ka = a.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "keys").value.split('|')[column];
			var kb = b.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "keys").value.split('|')[column];

			if (ka.match(/^s=/)) {
				ka = ka.substr(2).toLowerCase();
				kb = kb.substr(2).toLowerCase();
			} else if (ka.match(/^i=/)) {
				ka = parseInt(ka.substr(2));
				kb = parseInt(kb.substr(2));
			}

			var d = 0;
			if (ka < kb) {
				d = -1;
			} else if (ka > kb) {
				d = 1;
			}
			
			if (desc) {
				d = -d;
			}
			
			return d;
		});
		
		for (var i = 0; i < rowArray.length; ++i)
			t.appendChild(rowArray[i]);
	
		delete rowArray;
	}
}

// register a load handler
addLoadEvent(Status.init);
