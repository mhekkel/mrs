//---------------------------------------------------------------------
//
//	JavaScript for the Admin pages
//

Admin = {
	timeout: null,
	viewing: null,
	databank: null,

	init: function() {

		$("table.select_db tr").each(function() {
			if (this.id == Admin.databank)
				$(this).addClass('selected');
		
			$(this).click(function() {
				$("table.select_db tr").removeClass('selected');
				$(this).addClass('selected');
				var id = this.id;
				$("#databanks form.admin_form").hide();
				$("#databank-" + id).show();

				try {
					Admin.databank = id;
					sessionStorage.setItem('adminDb', id);
				} catch (e) {}
			})
		});

		try {
			Admin.databank = sessionStorage.getItem('adminDb');
			var v = sessionStorage.getItem('adminView');
			if (v == null) {
				v = 'databanks';
			}
			Admin.changeStatsView(v);
		} catch (e) { }
	},

	changeStatsView: function(view) {
		if (Admin.viewing == view)
			return;
		
		$("div.section").each(function() {
			if (this.id != view) { $(this).hide(); }
		});

		$("div.nav li a").each(function() {
			if (this.id == view)	{ $(this).addClass("selected"); }
			else					{ $(this).removeClass("selected"); }
		});
		
		$("div #" + view).show();
		
		if (view == "databanks" && Admin.databank != null)
		{
			$("#databanks form.admin_form").hide();
			$("#databank-" + Admin.databank).show();
		}

		try {
			Admin.viewing = view;
			sessionStorage.setItem('adminView', view);
		} catch (e) {}
	},
	
	updateStatus: function() {
		jQuery.getJSON("ajax/status", { method: Admin.viewing },
			function(data, status) {
				if (status == "success") 
				{
//					switch (Admin.viewing) {
//						case 'files': Admin.updateFiles(data); break;
//						case 'update': Admin.updateUpdate(data); break;
//					}
				}
				Admin.timeout = setTimeout("Admin.updateStatus()", 10000);
			}
		);
	},
	
	editProperty: function(db, prop) {
	    var propNode = document.getElementById(db + '-' + prop);

	    /* only when we're not in edit mode yet */
	    if (propNode.style.display == 'none')
	        return;
	
	    propNode.style.display = 'none';
	
	    var propEdit = document.getElementById('edit-' + db + '-' + prop);
	    propEdit.value = propNode.innerHTML;
	    propEdit.type = 'text';
	    propEdit.select();
	
	    propEdit.onblur = function () {
	        this.type = 'hidden';
	        propNode.style.display = '';
	    }
	
	    propEdit.onkeydown = function(e) {
	        var keyCode = e.keyCode;
	        if (keyCode == 27) {
	            this.type = 'hidden';
	            propNode.style.display = '';
	        }
	        else if (keyCode == 13) {
	            this.type = 'hidden';
				propNode.style.display = '';
	
				jQuery.getJSON("admin/rename", { db: db, prop: prop, value: this.value },
					function(data, status) {
						if (status == "success") {
							$(propNode).text(data.value);
						}
						else {
							alert(status);
						}
					}
				);
	        }
	        return true;
	    }
	}

}

// register a load handler
addLoadEvent(Admin.init);
