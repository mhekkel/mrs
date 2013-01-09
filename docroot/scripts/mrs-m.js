/* JavaScript for mobile version */

var M6M = {
	
	init: function(hitCount) {
		var results = $("#tabel");
		
		if (results != null && hitCount > 0)
		{
			$(window).scroll(function() {
				var pageHeight = document.documentElement.scrollHeight;
				var clientHeight = document.documentElement.clientHeight;
				if ((pageHeight - ($(window).scrollTop() + clientHeight)) < 50)
					M6M.extend(results, hitCount);
			});
		}
	},
	
	extend: function(results, hitCount) {
		alert(hitCount);
	}
};
