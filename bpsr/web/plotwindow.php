<?PHP


include("definitions_i.php");
include("functions_i.php");

$config = getConfigFile(SYS_CONFIG);
$conf = getConfigFile(DADA_CONFIG,TRUE);
$spec = getConfigFile(DADA_SPECIFICATION, TRUE);

$nbeam = $config["NUM_PWC"];

?>
<html>
<head>
  <? echo STYLESHEET_HTML; ?>
  <? echo FAVICO_HTML?>

  <script type="text/javascript">


    /* Creates a pop up window */
    function popWindow(URL,width,height) {

      var width = width || "1024";
      var height = height || "768";

      URL = URL + "&obsid=" + document.getElementById("utc_start").value;

      day = new Date();
      id = day.getTime();
      eval("page" + id + " = window.open(URL, '" + id + "', 'toolbar=1,scrollbars=1,location=1,statusbar=1,menubar=1,resizable=1,width="+width+",height="+height+"');");
    }

    /* Looping function to try and refresh the images */
    function looper() {
      request()
      setTimeout('looper()',5000)
    }

    /* Parses the HTTP response and makes changes to images
     * as requried */
    function handle_data(http_request) {

      if (http_request.readyState == 4) {
        var response = String(http_request.responseText)
        var lines = response.split(";;;")

<?
        for ($i=0; $i<$config["NUM_PWC"]; $i++) {
          echo "        var img".$i."_line = lines[".$i."].split(\":::\")\n";
        }

        for ($i=0; $i<$config["NUM_PWC"]; $i++) {
          echo "        var img".$i."_hires = img".$i."_line[1]\n";
          echo "        var img".$i."_lowres = img".$i."_line[2]\n";
          echo "        var img".$i." = document.getElementById(\"beam".($i+1)."\")\n";
        }

        for ($i=0; $i<$config["NUM_PWC"]; $i++) {

          echo "        if (img".$i.".src != img".$i."_lowres) {\n";
          echo "          img".$i.".src = img".$i."_lowres\n";
          //echo "          img".$i.".onmouseover = \"Tip('<img src=\"+img".$i."_hires+\" width=241 height=181>')\"\n";
          echo "        }\n";
        }
        
        echo "       var utc_start = lines[".$config["NUM_PWC"]."]\n";
?>
        document.getElementById("utc_start").value = utc_start;
      }
    }

    /* Gets the data from the URL */
    function request() {
      if (window.XMLHttpRequest)
        http_request = new XMLHttpRequest()
      else
        http_request = new ActiveXObject("Microsoft.XMLHTTP");
    
      http_request.onreadystatechange = function() {
        handle_data(http_request)
      }

      var type = "bandpass";

      if (document.imageform.imagetype[0].checked == true) {
        type = "bandpass";
      }

      if (document.imageform.imagetype[1].checked == true) {
        type = "dm0timeseries";
      }

      if (document.imageform.imagetype[2].checked == true) {
        type = "powerspectrum";
      }

      if (document.imageform.imagetype[3].checked == true) {
        type = "digitizer";
      }

      /* This URL will return the names of the 5 current */
      var url = "bpsr/plotupdate.php?results_dir=<?echo $config["SERVER_RESULTS_DIR"]?>&type="+type;

      http_request.open("GET", url, true)
      http_request.send(null)
    }


  </script>

</head>
<body onload="looper()">
<script type="text/javascript" src="/js/wz_tooltip.js"></script>
<input id="utc_start" type="hidden" value="">
<center>
<table border=0 cellspacing=5 cellpadding=5 width=100%>

  <tr>
    <td rowspan=3 valign="top">
      <form name="imageform" class="smalltext">
      <input type="radio" name="imagetype" id="imagetype" value="bandpass" checked onClick="request()">Bandpass<br>
      <input type="radio" name="imagetype" id="imagetype" value="dm0timeseries" onClick="request()">Time Series<br>
      <input type="radio" name="imagetype" id="imagetype" value="powerspectrum" onClick="request()">Fluct. Power Spectrum<br>
      <input type="radio" name="imagetype" id="imagetype" value="digitizer" onClick="request()">Digitizer Stats<br>
      </form>
    </td>
    <td colspan=4>

<div class="btns">
<?
for ($i=0; $i<$config["NUM_PWC"]; $i++) {
?>
  <a href="javascript:popWindow('bpsr/beamwindow.php?beamid=<?echo ($i+1)?>', 1024, 800)" class="btn" > <span><?echo ($i+1)?></span> </a>
<?
}
?>
</div>

    </td>
  </tr>

  <tr height=42>
    <?//echoBlank()?>
    <?echoBeam(13, $nbeam)?>
    <?echoBlank()?>
    <?echoBeam(12, $nbeam)?>
    <?echoBlank()?> 
  </tr>
  <tr height=42>
    <?//echoBlank()?>
    <?echoBeam(6, $nbeam)?>
    <?echoBlank()?>
  </tr>
  <tr height=42>
    <?echoBlank()?>
    <?echoBeam(7, $nbeam)?>
    <?echoBeam(5, $nbeam)?>
    <?echoBlank()?> 
  </tr>

  <tr height=42>
    <?echoBeam(8, $nbeam)?>
    <?echoBeam(1, $nbeam)?>
    <?echoBeam(11, $nbeam)?>
  </tr>

  <tr height=42>
    <?echoBeam(2, $nbeam)?>
    <?echoBeam(4, $nbeam)?>
  </tr>

  <tr height=42>
    <?echoBlank()?>
    <?echoBeam(3, $nbeam)?>
    <?echoBlank()?>
  </tr>

  <tr height=42>
    <?echoBlank()?>
    <?echoBeam(9, $nbeam)?>
    <?echoBeam(10, $nbeam)?>
    <?echoBlank()?>
  </tr>
  
  <tr height=42>
    <?echoBlank()?>
    <?echoBlank()?>
    <?echoBlank()?>
  </tr>
</table>
</center>



</body>
</html>

<?

function echoBlank() {

  echo "<td ></td>\n";
}

function echoBeam($beam_no, $num_beams) {

  if ($beam_no <= $num_beams) {
    //$mousein = "onmouseover=\"Tip('<img src=/images/blankimage.gif width=241 height=181>')\"";
    //$mouseout = "onmouseout=\"UnTip()\"";

    echo "<td rowspan=2 align=right>";
    echo "<a href=\"javascript:popWindow('bpsr/beamwindow.php?beamid=".$beam_no."')\">";

    echo "<img src=\"/images/blankimage.gif\" width=112 height=84 id=\"beam".$beam_no."\" TITLE=\"Beam ".$beam_no."\" alt=\"Beam ".$beam_no."\" ".$mousein." ".$mouseout.">\n";
    echo "</a></td>\n";
  } else {
    echo "<td rowspan=2></td>\n";
  }

}
